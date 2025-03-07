#include "../lfrfid_i.h"
#include <dolphin/dolphin.h>

typedef enum {
    SubmenuIndexASK,
    SubmenuIndexPSK,
    SubmenuIndexClearT5577,
    SubmenuIndexHitag,
    SubmenuIndexRAW,
    SubmenuIndexRAWEmulate,
} SubmenuIndex;

static void lfrfid_scene_extra_actions_submenu_callback(void* context, uint32_t index) {
    LfRfid* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void lfrfid_scene_extra_actions_on_enter(void* context) {
    LfRfid* app = context;
    Submenu* submenu = app->submenu;

    submenu_add_item(
        submenu,
        "Read ASK (FDX,Regular)",
        SubmenuIndexASK,
        lfrfid_scene_extra_actions_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Read PSK (Indala)",
        SubmenuIndexPSK,
        lfrfid_scene_extra_actions_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Clear T5577 Password",
        SubmenuIndexClearT5577,
        lfrfid_scene_extra_actions_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Read RTF (Reader Talks First)",
        SubmenuIndexHitag,
        lfrfid_scene_extra_actions_submenu_callback,
        app);

    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        submenu_add_item(
            submenu,
            "Read RAW RFID data",
            SubmenuIndexRAW,
            lfrfid_scene_extra_actions_submenu_callback,
            app);
        submenu_add_item(
            submenu,
            "Emulate RAW RFID data",
            SubmenuIndexRAWEmulate,
            lfrfid_scene_extra_actions_submenu_callback,
            app);
    }

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, LfRfidSceneExtraActions));

    // clear key
    furi_string_reset(app->file_name);
    app->protocol_id = PROTOCOL_NO;
    app->read_type = LFRFIDWorkerReadTypeAuto;

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewSubmenu);
}

bool lfrfid_scene_extra_actions_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexASK) {
            app->read_type = LFRFIDWorkerReadTypeASKOnly;
            scene_manager_next_scene(app->scene_manager, LfRfidSceneRead);
            dolphin_deed(DolphinDeedRfidRead);
            consumed = true;
        } else if(event.event == SubmenuIndexPSK) {
            app->read_type = LFRFIDWorkerReadTypePSKOnly;
            scene_manager_next_scene(app->scene_manager, LfRfidSceneRead);
            dolphin_deed(DolphinDeedRfidRead);
            consumed = true;
        } else if(event.event == SubmenuIndexClearT5577) {
            scene_manager_set_scene_state(
                app->scene_manager, LfRfidSceneEnterPassword, LfRfidSceneClearT5577Confirm);
            scene_manager_next_scene(app->scene_manager, LfRfidSceneEnterPassword);
            consumed = true;
        } else if(event.event == SubmenuIndexHitag) {
            app->read_type = LFRFIDWorkerReadTypeRTFOnly;
            scene_manager_next_scene(app->scene_manager, LfRfidSceneRead);
            dolphin_deed(DolphinDeedRfidRead);
            consumed = true;
        } else if(event.event == SubmenuIndexRAW) {
            scene_manager_next_scene(app->scene_manager, LfRfidSceneRawName);
            consumed = true;
        } else if(event.event == SubmenuIndexRAWEmulate) {
            scene_manager_next_scene(app->scene_manager, LfRfidSceneSelectRawKey);
            consumed = true;
        }
        scene_manager_set_scene_state(app->scene_manager, LfRfidSceneExtraActions, event.event);

    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_set_scene_state(app->scene_manager, LfRfidSceneExtraActions, 0);
    }

    return consumed;
}

void lfrfid_scene_extra_actions_on_exit(void* context) {
    LfRfid* app = context;

    submenu_reset(app->submenu);
}
