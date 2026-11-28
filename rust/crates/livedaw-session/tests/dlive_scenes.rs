//! A dLive's Scene Manager: Embedded Recall (Firmware Reference Guide 7.3) and Scene Import (7.6).

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::profile::{DLiveLayout, Profile};
use livedaw_engine::Engine;
use livedaw_session::console::NoPlugins;
use livedaw_session::show::{read_show_scenes, EmbeddedRecall, MAX_EMBEDDED_DELAY_MS};
use livedaw_session::Show;

fn dlive() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
    engine
}

fn show_of(engine: &Engine, names: &[&str]) -> Show {
    let mut show = Show::new();
    for name in names {
        show.add_scene(engine, &mut NoPlugins, name, -1);
    }
    show
}

fn embedded(show: &Show, scene: i32) -> Vec<(i32, u32)> {
    show.scene(scene).unwrap().embedded.iter().map(|e| (e.scene, e.delay_ms)).collect()
}

fn recall(scene: i32, delay_ms: u32) -> EmbeddedRecall {
    EmbeddedRecall { scene, delay_ms }
}

#[test]
fn embedded_recalls_are_kept_in_range_and_saved_with_the_show() {
    let engine = dlive();
    let mut show = show_of(&engine, &["A", "B", "C"]);
    // The scene itself drops out, a delay longer than four minutes is held there.
    show.set_scene_embedded(0, &[recall(2, 600), recall(0, 100), recall(1, 999_999)]);
    assert_eq!(embedded(&show, 0), vec![(2, 600), (1, MAX_EMBEDDED_DELAY_MS)]);

    let text = show.to_xml(&engine, &mut NoPlugins).to_document();
    assert_eq!(text.matches("embedded=").count(), 1, "left out while empty");
    let e2 = dlive();
    let mut loaded = Show::new();
    loaded.load_from_text(&e2, &mut NoPlugins, &text, true).unwrap();
    assert_eq!(embedded(&loaded, 0), vec![(2, 600), (1, MAX_EMBEDDED_DELAY_MS)]);
    assert!(loaded.scene(1).unwrap().embedded.is_empty());
}

#[test]
fn a_recall_hands_its_embedded_recalls_over_once() {
    let engine = dlive();
    let mut np = NoPlugins;
    let mut show = show_of(&engine, &["A", "B", "C", "D"]);
    show.set_scene_embedded(0, &[recall(3, 0)]);
    show.set_scene_embedded(1, &[recall(2, 1500)]);

    show.recall_scene(&engine, &mut np, 0);
    assert_eq!(show.take_pending_embedded(), vec![recall(3, 0)]);
    assert!(show.take_pending_embedded().is_empty(), "taken once");

    // A linked scene's come after the first's; a scene without any leaves none over.
    show.scene_mut(0).unwrap().link_next = true;
    show.recall_scene(&engine, &mut np, 0);
    assert_eq!(show.take_pending_embedded(), vec![recall(3, 0), recall(2, 1500)]);
    show.recall_scene(&engine, &mut np, 0);
    show.recall_scene(&engine, &mut np, 3);
    assert!(show.take_pending_embedded().is_empty());
}

#[test]
fn embedded_recalls_follow_their_scenes() {
    let engine = dlive();
    let mut np = NoPlugins;
    let mut show = show_of(&engine, &["A", "B", "C", "D"]);
    show.set_scene_embedded(0, &[recall(2, 10), recall(3, 20)]);

    // A scene inserted before them moves them on.
    show.add_scene(&engine, &mut np, "New", 1);
    assert_eq!(embedded(&show, 0), vec![(3, 10), (4, 20)]);
    // A move takes them along.
    show.move_scene(4, 1);
    assert_eq!(embedded(&show, 0), vec![(4, 10), (1, 20)]);
    assert_eq!(show.scene(1).unwrap().name, "D");
    // A removed scene drops out, the others close up.
    show.remove_scene(1);
    assert_eq!(embedded(&show, 0), vec![(3, 10)]);
    assert_eq!(show.scene(3).unwrap().name, "C");
}

#[test]
fn scene_import_takes_chosen_scenes_of_another_show() {
    let engine = dlive();
    let dir = std::env::temp_dir().join(format!("sidecar-dlive-import-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("guest.lshow");

    let mut guest = show_of(&engine, &["Band 1", "Band 2", "Band 3"]);
    guest.set_scene_embedded(1, &[recall(2, 0)]);
    guest.scene_mut(1).unwrap().fade_seconds = 2.5;
    guest.save_to_file(&engine, &mut NoPlugins, &path).unwrap();

    let file = read_show_scenes(&path).unwrap();
    assert_eq!(file.scenes.iter().map(|s| s.name.as_str()).collect::<Vec<_>>(), vec!["Band 1", "Band 2", "Band 3"]);
    assert!(file.scenes.iter().all(|s| s.state.is_some()), "with their console states");

    let mut host = show_of(&engine, &["Ours 1", "Ours 2"]);
    host.recall_scene(&engine, &mut NoPlugins, 1);
    let first = host.import_show_scenes(&engine, &path, &[2, 1, 7], 1).unwrap();
    assert_eq!(first, 1);
    let names: Vec<String> = (0..host.num_scenes() as i32).map(|i| host.scene(i).unwrap().name.clone()).collect();
    assert_eq!(names, vec!["Ours 1", "Band 3", "Band 2", "Ours 2"]);
    assert_eq!(host.scene(2).unwrap().fade_seconds, 2.5);
    assert!(host.scene(2).unwrap().embedded.is_empty(), "the other show's scene numbers stay behind");
    assert!(host.scene(1).unwrap().state.is_some());
    assert_eq!(host.current_scene(), 3, "the last recalled scene moves down with the import");

    // Nothing chosen, no file, or another console's show: nothing changes.
    assert!(host.import_show_scenes(&engine, &path, &[9], 0).is_err());
    assert!(host.import_show_scenes(&engine, &dir.join("missing.lshow"), &[0], 0).is_err());
    let other = Engine::new(0);
    let wing_path = dir.join("wing.lshow");
    show_of(&other, &["W"]).save_to_file(&other, &mut NoPlugins, &wing_path).unwrap();
    assert!(host.import_show_scenes(&engine, &wing_path, &[0], 0).is_err());
    assert_eq!(host.num_scenes(), 4);
    let _ = std::fs::remove_dir_all(&dir);
}
