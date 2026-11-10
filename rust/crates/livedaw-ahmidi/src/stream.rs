//! MIDI messages out of a TCP byte stream: a message may arrive in pieces, and the
//! consoles use running status (the dLive's document says so outright; a client may
//! leave status bytes out).

/// The longest SysEx kept (a name is a few characters; a longer one is dropped whole).
pub const MAX_SYSEX: usize = 4096;

/// Assembles MIDI messages from a byte stream, keeping what's left of a message
/// between reads. One per connection.
#[derive(Debug, Default)]
pub struct Parser {
    /// The running status (0: none, data bytes are ignored).
    status: u8,
    data: [u8; 2],
    have: usize,
    /// The SysEx being received.
    sysex: Option<Vec<u8>>,
    /// The SysEx in progress grew past [`MAX_SYSEX`]: dropped when it ends.
    overflow: bool,
}

/// The data bytes of a channel message's status.
fn data_len(status: u8) -> usize {
    match status & 0xF0 {
        0xC0 | 0xD0 => 1,
        _ => 2,
    }
}

impl Parser {
    /// Takes one byte; `out` gets each message it completes (a channel message with
    /// its status always written out, a SysEx from F0 to F7).
    pub fn push(&mut self, b: u8, out: &mut impl FnMut(&[u8])) {
        // Realtime bytes may fall anywhere and change nothing.
        if b >= 0xF8 {
            return;
        }
        if let Some(sx) = &mut self.sysex {
            if b == 0xF7 {
                if !self.overflow {
                    sx.push(b);
                    out(sx);
                }
                self.sysex = None;
                self.overflow = false;
                return;
            }
            if b < 0x80 {
                if sx.len() >= MAX_SYSEX {
                    self.overflow = true;
                } else {
                    sx.push(b);
                }
                return;
            }
            // Another status byte: the SysEx never ended. It's dropped; the byte stands.
            self.sysex = None;
            self.overflow = false;
        }
        match b {
            0xF0 => {
                self.status = 0;
                self.sysex = Some(vec![0xF0]);
            }
            // A stray end, or system common: its data bytes (none are used) are ignored too.
            0xF1..=0xF7 => self.status = 0,
            0x80..=0xEF => {
                self.status = b;
                self.have = 0;
            }
            _ => {
                if self.status == 0 {
                    return; // a data byte with no status
                }
                self.data[self.have] = b;
                self.have += 1;
                let need = data_len(self.status);
                if self.have == need {
                    self.have = 0;
                    let msg = [self.status, self.data[0], self.data[1]];
                    out(&msg[..1 + need]);
                }
            }
        }
    }

    /// Takes a whole read.
    pub fn feed(&mut self, bytes: &[u8], out: &mut impl FnMut(&[u8])) {
        for &b in bytes {
            self.push(b, out);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(bytes: &[u8]) -> Vec<Vec<u8>> {
        let mut p = Parser::default();
        let mut got = Vec::new();
        p.feed(bytes, &mut |m| got.push(m.to_vec()));
        got
    }

    #[test]
    fn running_status_as_the_document_shows_it() {
        // 9B, 00, 7F, 01, 7F, 02, 7F
        assert_eq!(
            parse(&[0x9B, 0x00, 0x7F, 0x01, 0x7F, 0x02, 0x7F]),
            vec![vec![0x9B, 0x00, 0x7F], vec![0x9B, 0x01, 0x7F], vec![0x9B, 0x02, 0x7F]]
        );
    }

    #[test]
    fn a_message_in_pieces_across_reads() {
        let mut p = Parser::default();
        let mut got = Vec::new();
        for chunk in [&[0xB0u8][..], &[0x63], &[0x05, 0xB0, 0x62], &[0x17, 0x06]] {
            p.feed(chunk, &mut |m| got.push(m.to_vec()));
        }
        assert_eq!(got, vec![vec![0xB0, 0x63, 0x05], vec![0xB0, 0x62, 0x17]]);
        p.feed(&[0x40], &mut |m| got.push(m.to_vec()));
        assert_eq!(got.last().unwrap(), &vec![0xB0, 0x06, 0x40]);
    }

    #[test]
    fn program_change_takes_one_data_byte_and_realtime_changes_nothing() {
        assert_eq!(parse(&[0xC0, 0x05, 0xF8, 0x06]), vec![vec![0xC0, 0x05], vec![0xC0, 0x06]]);
        assert_eq!(parse(&[0x9B, 0x00, 0xFE, 0x7F]), vec![vec![0x9B, 0x00, 0x7F]]);
    }

    #[test]
    fn sysex_ends_at_f7_and_a_broken_one_is_dropped() {
        assert_eq!(parse(&[0xF0, 1, 2, 0xF7, 0x90, 1, 2]), vec![vec![0xF0, 1, 2, 0xF7], vec![0x90, 1, 2]]);
        // The next status byte cuts it short.
        assert_eq!(parse(&[0xF0, 1, 2, 0x90, 1, 2]), vec![vec![0x90, 1, 2]]);
        // Too long: nothing comes out for it, and the stream carries on.
        let mut long = vec![0xF0];
        long.resize(MAX_SYSEX + 10, 1);
        long.push(0xF7);
        long.extend([0x90, 3, 4]);
        assert_eq!(parse(&long), vec![vec![0x90, 3, 4]]);
    }

    #[test]
    fn data_with_no_status_is_ignored() {
        assert_eq!(parse(&[0x01, 0x02, 0x90, 1, 2]), vec![vec![0x90, 1, 2]]);
        // System common cancels running status.
        assert_eq!(parse(&[0x90, 1, 2, 0xF1, 3, 4, 5]), vec![vec![0x90, 1, 2]]);
    }
}
