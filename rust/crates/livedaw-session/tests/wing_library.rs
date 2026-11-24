//! The WING's LIBRARY (issue #31): snaps are files on INT or USB, saved,
//! updated and loaded without a show, and a show is a list of links to them.
//! CREATE, OPEN and CLOSE leave the console as it is.

use livedaw_engine::Engine;
use livedaw_session::library;
use livedaw_session::*;
use std::path::PathBuf;

fn engine_with_channels(n: usize) -> Engine {
    let e = Engine::new(0);
    e.set_num_channels(n);
    e
}

fn fader(e: &Engine) -> f32 {
    e.channel_at(1).unwrap().params.fader_db.load()
}

fn set_fader(e: &Engine, db: f32) {
    e.channel_at(1).unwrap().params.fader_db.store(db);
}

struct TempDir(PathBuf);

impl TempDir {
    fn new(name: &str) -> Self {
        let dir = std::env::temp_dir().join(format!("livedaw-wing-library-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        Self(dir)
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

#[test]
fn snap_files_work_without_a_show() {
    let dir = TempDir::new("files");
    let e = engine_with_channels(2);
    let mut show = Show::new();
    let snap = dir.0.join("INT").join("Verse.lsnap");

    // SAVE at INT: a file, and the show has nothing new.
    set_fader(&e, -10.0);
    show.save_scene_file(&e, &mut NoPlugins, &snap, "Verse", None).unwrap();
    assert!(snap.is_file());
    assert_eq!((show.scenes().len(), show.current_scene()), (0, -1));
    assert_eq!(library::read_scene_file(&snap).unwrap().name, "Verse");

    // LOAD.
    set_fader(&e, 0.0);
    show.recall_scene_file(&e, &mut NoPlugins, &snap, None, false, false).unwrap();
    assert_eq!(fader(&e), -10.0);

    // UPDATE.
    set_fader(&e, -20.0);
    show.update_scene_file(&e, &mut NoPlugins, &snap, None).unwrap();
    set_fader(&e, 0.0);
    show.recall_scene_file(&e, &mut NoPlugins, &snap, None, false, false).unwrap();
    assert_eq!(fader(&e), -20.0);

    // EDIT SCOPE: nothing in it, so LOAD leaves the fader; RESTORE loads it all.
    show.set_scene_file_scope(&snap, &Scope::console(0)).unwrap();
    set_fader(&e, 0.0);
    show.recall_scene_file(&e, &mut NoPlugins, &snap, None, false, false).unwrap();
    assert_eq!(fader(&e), 0.0);
    show.recall_scene_file(&e, &mut NoPlugins, &snap, None, false, true).unwrap();
    assert_eq!(fader(&e), -20.0);

    // PARTIAL LOAD of the faders only.
    set_fader(&e, 0.0);
    show.recall_scene_file(&e, &mut NoPlugins, &snap, Some(&Scope::console(section::ALL)), false, false).unwrap();
    assert_eq!(fader(&e), -20.0);
    assert_eq!(show.scenes().len(), 0, "the show is never touched");

    assert!(show.recall_scene_file(&e, &mut NoPlugins, &dir.0.join("missing.lsnap"), None, false, false).is_err());
}

#[test]
fn a_show_links_to_snap_files() {
    let dir = TempDir::new("links");
    let e = engine_with_channels(2);
    let mut show = Show::new();
    let snap = dir.0.join("CONCERT").join("Intro.lsnap");

    // SAVE + ADD: a scene linked to its file.
    set_fader(&e, -10.0);
    let index = show.add_scene(&e, &mut NoPlugins, "Intro", -1);
    show.set_scene_file(index, Some(snap.clone()));
    show.write_linked(index).unwrap();
    assert!(snap.is_file());

    // UPDATE of the scene writes the file.
    set_fader(&e, -5.0);
    show.store_scene(&e, &mut NoPlugins, index);
    set_fader(&e, 0.0);
    show.recall_scene_file(&e, &mut NoPlugins, &snap, None, false, false).unwrap();
    assert_eq!(fader(&e), -5.0);

    // UPDATE of the file at INT: the show's scene follows.
    set_fader(&e, -7.0);
    show.update_scene_file(&e, &mut NoPlugins, &snap, None).unwrap();
    set_fader(&e, 0.0);
    show.recall_scene(&e, &mut NoPlugins, index);
    assert_eq!(fader(&e), -7.0);

    // The link is saved with the show; OPEN takes the scenes and leaves the console.
    let show_file = dir.0.join("CONCERT.lshow");
    show.name = "CONCERT".into();
    show.save_to_file(&e, &mut NoPlugins, &show_file).unwrap();
    assert!(std::fs::read_to_string(&show_file).unwrap().contains("Intro.lsnap"));
    set_fader(&e, -3.0);
    show.update_scene_file(&e, &mut NoPlugins, &snap, None).unwrap(); // the file changes after the save
    set_fader(&e, 2.0);
    let mut opened = Show::new();
    opened.load_scenes_from_file(&e, &show_file).unwrap();
    assert_eq!(fader(&e), 2.0, "OPEN recalls nothing");
    assert_eq!((opened.name.as_str(), opened.scenes().len(), opened.current_scene()), ("CONCERT", 1, -1));
    assert_eq!(opened.scene(0).and_then(|s| s.file.clone()), Some(snap.clone()));
    opened.recall_scene(&e, &mut NoPlugins, 0);
    assert_eq!(fader(&e), -3.0, "from the file as it is now");

    // A missing file: the show's copy.
    std::fs::remove_file(&snap).unwrap();
    assert_eq!(opened.refresh_linked(None), 0);
    set_fader(&e, 2.0);
    opened.recall_scene(&e, &mut NoPlugins, 0);
    assert_eq!(fader(&e), -3.0);

    // CLOSE / CREATE: no scenes, the console as it is.
    opened.clear_scenes(&e, "NEW");
    assert_eq!((opened.name.as_str(), opened.scenes().len(), opened.current_scene()), ("NEW", 0, -1));
    assert_eq!(fader(&e), -3.0);
    assert_eq!(e.channels().len(), 2);
}
