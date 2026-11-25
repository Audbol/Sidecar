//! PARTIAL UPDATE stores only what its scope covers into a scene.

use livedaw_engine::{Engine, GroupSet};
use livedaw_session::*;

#[test]
fn a_partial_update_of_some_mute_groups_leaves_the_others_on_or_off_as_stored() {
    let e = Engine::new(0);
    e.set_num_channels(2);
    let mut show = Show::new();
    let groups = |set: GroupSet| e.controls.mute_groups.store(set);
    let on = || e.controls.mute_groups.load();

    // Stored with MG1 and MG2 off.
    groups(GroupSet::EMPTY);
    let scene = show.add_scene(&e, &mut NoPlugins, "Scene", -1);

    // MG2 goes on at the console; MG1 too, and a PARTIAL UPDATE takes MG1 only.
    groups(GroupSet::single(0) | GroupSet::single(1));
    let scope = Scope { mute_groups: GroupSet::single(0), ..Scope::strips(section::GROUPS, Vec::new()) };
    show.store_scene_partial(&e, &mut NoPlugins, scene, &scope);

    groups(GroupSet::EMPTY);
    show.recall_scene(&e, &mut NoPlugins, scene);
    assert_eq!(on(), GroupSet::single(0), "MG1 as updated, MG2 off as stored");
}

#[test]
fn a_partial_routing_update_back_to_the_default_mix_input_sticks() {
    use std::sync::atomic::Ordering::Relaxed;
    let e = Engine::new(0);
    e.set_num_channels(2);
    let bus = e.strip_at(livedaw_engine::StripKind::Bus, 0).unwrap();
    let mut show = Show::new();

    // Stored with the bus's mix input from source 5.
    bus.mix_input.ext_source.store(5, Relaxed);
    let scene = show.add_scene(&e, &mut NoPlugins, "Scene", -1);
    // A scene recalls routing only with it in its scope (not by default, as on a WING).
    show.scene_mut(scene).unwrap().set_recall_scope(&Scope::console(section::ALL));

    // Back to no source, and a PARTIAL UPDATE of the bus's routing.
    bus.mix_input.ext_source.store(-1, Relaxed);
    let scope = Scope::strips(section::ROUTING, vec![StripRef::of(&bus)]);
    show.store_scene_partial(&e, &mut NoPlugins, scene, &scope);

    bus.mix_input.ext_source.store(7, Relaxed);
    show.recall_scene(&e, &mut NoPlugins, scene);
    assert_eq!(bus.mix_input.ext_source.load(Relaxed), -1, "not the source 5 from before the update");
}
