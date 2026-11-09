//! The WING's Native protocol carried in a WebSocket (RFC 6455) on the Native TCP
//! port, for clients that are web pages (WING-Q runs in an Android WebView, which
//! can't open a plain TCP socket). The messages' bytes are the Native stream as it
//! would be on TCP: `native` handles them the same way.
//!
//! A web page can open a WebSocket to any address, so the upgrade is only granted
//! to a page the user's own device serves (an app's WebView: `http://localhost`) or
//! to a client that isn't a browser (no `Origin`): any web site opened in a browser
//! on the network must not be able to drive the console.

/// What a connection's first bytes are.
#[derive(Debug, PartialEq, Eq)]
pub enum Opening {
    /// Not enough yet to tell.
    Undecided,
    /// The Native protocol, from the first byte.
    Native,
    /// An HTTP request, whose header is the first `n` bytes.
    Http(usize),
}

/// An HTTP request's header longer than this isn't one.
const MAX_HEADER: usize = 16 * 1024;
/// A message from a client is a few Native tokens: anything like this is not.
const MAX_MESSAGE: u64 = 1 << 20;

pub const OP_CONTINUATION: u8 = 0;
pub const OP_BINARY: u8 = 2;
pub const OP_CLOSE: u8 = 8;
pub const OP_PING: u8 = 9;
pub const OP_PONG: u8 = 10;

const GUID: &[u8] = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// Tells what `bytes`, all a connection has sent so far, are.
pub fn classify(bytes: &[u8]) -> Opening {
    const GET: &[u8] = b"GET ";
    if bytes.len() < GET.len() {
        return if GET.starts_with(bytes) { Opening::Undecided } else { Opening::Native };
    }
    if !bytes.starts_with(GET) {
        return Opening::Native;
    }
    match bytes.windows(4).position(|w| w == b"\r\n\r\n") {
        Some(at) => Opening::Http(at + 4),
        None if bytes.len() > MAX_HEADER => Opening::Native,
        None => Opening::Undecided,
    }
}

fn header<'a>(request: &'a str, name: &str) -> Option<&'a str> {
    request.split("\r\n").skip(1).find_map(|line| {
        let (key, value) = line.split_once(':')?;
        key.trim().eq_ignore_ascii_case(name).then(|| value.trim())
    })
}

/// Whether a browser page from `origin` may open the console's WebSocket: a page
/// served by the device itself, never another site.
fn origin_allowed(origin: &str) -> bool {
    let Some((scheme, rest)) = origin.split_once("://") else { return false };
    if scheme == "file" {
        return true;
    }
    let host = rest.split('/').next().unwrap_or("");
    let host = if let Some(v6) = host.strip_prefix('[') {
        v6.split(']').next().unwrap_or("")
    } else {
        host.split(':').next().unwrap_or("")
    };
    matches!(host, "localhost" | "127.0.0.1" | "::1")
}

/// The answer to an HTTP request: `Ok` the 101 that opens the WebSocket, `Err` the
/// refusal to send before closing.
pub fn handshake(request: &[u8]) -> Result<Vec<u8>, Vec<u8>> {
    let refuse = |status: &str| format!("HTTP/1.1 {status}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n").into_bytes();
    let Ok(request) = std::str::from_utf8(request) else { return Err(refuse("400 Bad Request")) };
    let wants_websocket = header(request, "Upgrade").is_some_and(|v| v.eq_ignore_ascii_case("websocket"));
    let Some(key) = header(request, "Sec-WebSocket-Key").filter(|_| wants_websocket) else {
        return Err(refuse("400 Bad Request"));
    };
    if header(request, "Origin").is_some_and(|o| !origin_allowed(o)) {
        return Err(refuse("403 Forbidden"));
    }
    let mut input = key.as_bytes().to_vec();
    input.extend_from_slice(GUID);
    let accept = base64(&sha1(&input));
    // No extensions (permessage-deflate) offered back: the messages are small.
    Ok(format!(
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n"
    )
    .into_bytes())
}

/// A server's frame (not masked, RFC 6455 5.1) of `payload`.
pub fn frame(opcode: u8, payload: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(payload.len() + 10);
    out.push(0x80 | opcode);
    match payload.len() {
        n if n < 126 => out.push(n as u8),
        n if n <= 0xffff => {
            out.push(126);
            out.extend_from_slice(&(n as u16).to_be_bytes());
        }
        n => {
            out.push(127);
            out.extend_from_slice(&(n as u64).to_be_bytes());
        }
    }
    out.extend_from_slice(payload);
    out
}

/// The most Native bytes one message carries. WING-Q drops a very large message
/// (the whole console, 228 KB, in one) without a word and waits on for it; the
/// bytes are a stream to it, so a message can end anywhere.
pub const MAX_MESSAGE_BYTES: usize = 1024;

/// The Native bytes `bytes` as binary messages, of at most [`MAX_MESSAGE_BYTES`] each.
pub fn messages(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(bytes.len() + 4 * (bytes.len() / MAX_MESSAGE_BYTES + 1));
    for part in bytes.chunks(MAX_MESSAGE_BYTES) {
        out.extend_from_slice(&frame(OP_BINARY, part));
    }
    out
}

/// What some received bytes came to.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct Received {
    /// The Native bytes the data frames carried.
    pub payload: Vec<u8>,
    /// Frames to send back (pongs, the close's answer).
    pub replies: Vec<Vec<u8>>,
    /// The client closed the WebSocket or broke its framing.
    pub closed: bool,
}

/// Takes a client's frames apart, however the TCP reads cut them.
#[derive(Debug, Default)]
pub struct Reader {
    pending: Vec<u8>,
}

impl Reader {
    pub fn push(&mut self, bytes: &[u8]) -> Received {
        self.pending.extend_from_slice(bytes);
        let mut out = Received::default();
        let mut used = 0;
        while !out.closed {
            match parse(&self.pending[used..]) {
                Parsed::More => break,
                Parsed::Bad => out.closed = true,
                Parsed::Frame { opcode, payload, len } => {
                    used += len;
                    match opcode {
                        OP_PING => out.replies.push(frame(OP_PONG, &payload)),
                        OP_PONG => {}
                        OP_CLOSE => {
                            out.replies.push(frame(OP_CLOSE, &payload));
                            out.closed = true;
                        }
                        // Text is only ever a mistake here; its bytes go the same way.
                        _ => out.payload.extend_from_slice(&payload),
                    }
                }
            }
        }
        self.pending.drain(..used);
        out
    }
}

enum Parsed {
    More,
    Bad,
    Frame { opcode: u8, payload: Vec<u8>, len: usize },
}

fn parse(b: &[u8]) -> Parsed {
    if b.len() < 2 {
        return Parsed::More;
    }
    let opcode = b[0] & 0x0f;
    let masked = b[1] & 0x80 != 0;
    let mut at = 2;
    let length = match b[1] & 0x7f {
        126 => {
            if b.len() < 4 {
                return Parsed::More;
            }
            at = 4;
            u16::from_be_bytes([b[2], b[3]]) as u64
        }
        127 => {
            if b.len() < 10 {
                return Parsed::More;
            }
            at = 10;
            u64::from_be_bytes([b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]])
        }
        n => n as u64,
    };
    if length > MAX_MESSAGE {
        return Parsed::Bad;
    }
    let mask_at = at;
    if masked {
        at += 4;
    }
    let end = at + length as usize;
    if b.len() < end {
        return Parsed::More;
    }
    let mut payload = b[at..end].to_vec();
    if masked {
        let mask = &b[mask_at..mask_at + 4];
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte ^= mask[i % 4];
        }
    }
    Parsed::Frame { opcode, payload, len: end }
}

/// SHA-1 (FIPS 180-1), which the handshake's accept value is defined with.
fn sha1(message: &[u8]) -> [u8; 20] {
    let mut h: [u32; 5] = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0];
    let mut data = message.to_vec();
    data.push(0x80);
    while data.len() % 64 != 56 {
        data.push(0);
    }
    data.extend_from_slice(&((message.len() as u64) * 8).to_be_bytes());
    for block in data.chunks_exact(64) {
        let mut w = [0u32; 80];
        for (i, word) in block.chunks_exact(4).enumerate() {
            w[i] = u32::from_be_bytes([word[0], word[1], word[2], word[3]]);
        }
        for i in 16..80 {
            w[i] = (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]).rotate_left(1);
        }
        let [mut a, mut b, mut c, mut d, mut e] = h;
        for (i, &word) in w.iter().enumerate() {
            let (f, k) = match i {
                0..=19 => ((b & c) | (!b & d), 0x5A827999),
                20..=39 => (b ^ c ^ d, 0x6ED9EBA1),
                40..=59 => ((b & c) | (b & d) | (c & d), 0x8F1BBCDC),
                _ => (b ^ c ^ d, 0xCA62C1D6u32),
            };
            let next = a.rotate_left(5).wrapping_add(f).wrapping_add(e).wrapping_add(k).wrapping_add(word);
            e = d;
            d = c;
            c = b.rotate_left(30);
            b = a;
            a = next;
        }
        for (v, add) in h.iter_mut().zip([a, b, c, d, e]) {
            *v = v.wrapping_add(add);
        }
    }
    let mut out = [0u8; 20];
    for (chunk, v) in out.chunks_exact_mut(4).zip(h) {
        chunk.copy_from_slice(&v.to_be_bytes());
    }
    out
}

fn base64(bytes: &[u8]) -> String {
    const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let n = (chunk[0] as u32) << 16 | (*chunk.get(1).unwrap_or(&0) as u32) << 8 | *chunk.get(2).unwrap_or(&0) as u32;
        out.push(TABLE[(n >> 18) as usize & 63] as char);
        out.push(TABLE[(n >> 12) as usize & 63] as char);
        out.push(if chunk.len() > 1 { TABLE[(n >> 6) as usize & 63] as char } else { '=' });
        out.push(if chunk.len() > 2 { TABLE[n as usize & 63] as char } else { '=' });
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn upgrade(origin: Option<&str>) -> Vec<u8> {
        let origin = origin.map_or(String::new(), |o| format!("Origin: {o}\r\n"));
        format!("GET / HTTP/1.1\r\nHost: x:2222\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n{origin}Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n").into_bytes()
    }

    /// A client's frame: masked, as RFC 6455 requires.
    fn masked(opcode: u8, fin: bool, payload: &[u8]) -> Vec<u8> {
        let mask = [0x37, 0xfa, 0x21, 0x3d];
        let mut out = vec![(if fin { 0x80 } else { 0 }) | opcode, 0x80 | payload.len() as u8];
        out.extend_from_slice(&mask);
        out.extend(payload.iter().enumerate().map(|(i, b)| b ^ mask[i % 4]));
        out
    }

    #[test]
    fn the_accept_value_is_the_rfcs_example() {
        let response = String::from_utf8(handshake(&upgrade(None)).unwrap()).unwrap();
        assert!(response.starts_with("HTTP/1.1 101 "), "{response}");
        assert!(response.contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"), "{response}");
        assert!(!response.contains("Extensions"), "nothing compressed");
    }

    #[test]
    fn an_app_webview_may_connect_but_no_web_site_may() {
        for ok in [
            "http://localhost",
            "https://localhost",
            "capacitor://localhost",
            "ionic://localhost",
            "http://127.0.0.1:8080",
            "http://[::1]:3000",
            "file://",
        ] {
            assert!(handshake(&upgrade(Some(ok))).is_ok(), "{ok}");
        }
        for bad in ["http://evil.example", "https://localhost.evil.example", "http://192.168.1.20", "null"] {
            let refusal = handshake(&upgrade(Some(bad))).unwrap_err();
            assert!(refusal.starts_with(b"HTTP/1.1 403"), "{bad}");
        }
    }

    #[test]
    fn a_plain_get_is_refused() {
        let refusal = handshake(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n").unwrap_err();
        assert!(refusal.starts_with(b"HTTP/1.1 400"));
    }

    #[test]
    fn the_first_bytes_tell_the_protocols_apart() {
        assert_eq!(classify(b""), Opening::Undecided);
        assert_eq!(classify(b"GE"), Opening::Undecided);
        assert_eq!(classify(&[0xdf, 0xd1]), Opening::Native);
        assert_eq!(classify(b"GEX"), Opening::Native);
        assert_eq!(classify(b"GET / HTTP/1.1\r\n"), Opening::Undecided);
        let request = upgrade(None);
        assert_eq!(classify(&request), Opening::Http(request.len()));
    }

    #[test]
    fn frames_are_taken_apart_however_they_arrive() {
        // WING-Q's opening: a channel select, then a node and a data request.
        let mut wire = masked(OP_BINARY, true, &[0xdf, 0xd1]);
        wire.extend(masked(OP_BINARY, true, &[0xd7, 0xf8, 0x98, 0x18, 0xa6, 0xdc]));
        let mut reader = Reader::default();
        let mut payload = Vec::new();
        for byte in &wire {
            let got = reader.push(std::slice::from_ref(byte));
            assert!(!got.closed && got.replies.is_empty());
            payload.extend(got.payload);
        }
        assert_eq!(payload, [0xdf, 0xd1, 0xd7, 0xf8, 0x98, 0x18, 0xa6, 0xdc]);
    }

    #[test]
    fn a_message_in_fragments_comes_out_whole() {
        let mut wire = masked(OP_BINARY, false, &[1, 2]);
        wire.extend(masked(OP_CONTINUATION, true, &[3]));
        assert_eq!(Reader::default().push(&wire).payload, [1, 2, 3]);
    }

    #[test]
    fn pings_are_answered_and_a_close_ends_it() {
        let mut reader = Reader::default();
        let got = reader.push(&masked(OP_PING, true, b"hi"));
        assert_eq!(got.replies, vec![frame(OP_PONG, b"hi")]);
        assert!(!got.closed);
        let got = reader.push(&masked(OP_CLOSE, true, &[0x03, 0xe8]));
        assert_eq!(got.replies, vec![frame(OP_CLOSE, &[0x03, 0xe8])]);
        assert!(got.closed);
    }

    #[test]
    fn an_absurd_length_closes_the_connection() {
        let mut wire = vec![0x82, 0xff];
        wire.extend_from_slice(&u64::MAX.to_be_bytes());
        assert!(Reader::default().push(&wire).closed);
    }

    #[test]
    fn frames_use_the_length_form_they_need() {
        assert_eq!(frame(OP_BINARY, &[9]), [0x82, 1, 9]);
        assert_eq!(&frame(OP_BINARY, &[0; 126])[..4], &[0x82, 126, 0, 126]);
        assert_eq!(&frame(OP_BINARY, &[0; 70000])[..10], &[0x82, 127, 0, 0, 0, 0, 0, 1, 0x11, 0x70]);
    }

    #[test]
    fn a_long_stream_goes_out_in_small_messages() {
        let stream: Vec<u8> = (0..5000u32).map(|i| i as u8).collect();
        let wire = messages(&stream);
        let (mut at, mut back, mut count) = (0, Vec::new(), 0);
        while at < wire.len() {
            assert_eq!(wire[at], 0x82, "a final binary frame");
            let (len, head) = match wire[at + 1] {
                126 => (u16::from_be_bytes([wire[at + 2], wire[at + 3]]) as usize, 4),
                n => (n as usize, 2),
            };
            assert!(len <= MAX_MESSAGE_BYTES);
            back.extend_from_slice(&wire[at + head..at + head + len]);
            at += head + len;
            count += 1;
        }
        assert_eq!(back, stream);
        assert_eq!(count, 5);
    }

    #[test]
    fn sha1_and_base64_match_their_standards() {
        assert_eq!(base64(&sha1(b"abc")), "qZk+NkcGgWq6PiVxeFDCbJzQ2J0=");
        assert_eq!(base64(b"f"), "Zg==");
        assert_eq!(base64(b"fo"), "Zm8=");
        assert_eq!(base64(b"foo"), "Zm9v");
    }
}
