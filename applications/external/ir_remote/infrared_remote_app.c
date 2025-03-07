#include <furi.h>
#include <furi_hal.h>

#include <infrared_worker.h>

#include <gui/gui.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <ir_remote_icons.h>

#include <infrared/infrared_app.h>
#include <toolbox/saved_struct.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "infrared_signal.h"
#include "infrared_remote.h"
#include "infrared_remote_button.h"
#define TAG "ir_remote"
#define MENU_BTN_TXT_X 36

#include <flipper_format/flipper_format.h>

struct InfraredSignal {
    bool is_raw;
    union {
        InfraredMessage message;
        InfraredRawSignal raw;
    } payload;
};

typedef struct {
    int status;
    ViewPort* view_port;
    FuriString* up_button;
    FuriString* down_button;
    FuriString* left_button;
    FuriString* right_button;
    FuriString* ok_button;
    FuriString* back_button;
    FuriString* up_hold_button;
    FuriString* down_hold_button;
    FuriString* left_hold_button;
    FuriString* right_hold_button;
    FuriString* ok_hold_button;
    bool repeat_signal;
    InfraredWorker* infrared_worker;
} IRApp;

// Screen is 128x64 px
static void app_draw_callback(Canvas* canvas, void* ctx) {
    // Show config is incorrect when cannot read the remote file
    // Showing button string in the screen, upper part is short press, lower part is long press
    IRApp* app = ctx;
    if(app->status) {
        canvas_clear(canvas);
        view_port_set_orientation(app->view_port, ViewPortOrientationHorizontal);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 62, 5, AlignCenter, AlignTop, "Config is incorrect.");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 62, 30, AlignCenter, AlignTop, "Please configure map.");
        canvas_draw_str_aligned(canvas, 62, 60, AlignCenter, AlignBottom, "Press Back to Exit.");
    } else {
        canvas_clear(canvas);
        view_port_set_orientation(app->view_port, ViewPortOrientationVertical);
        canvas_draw_icon(canvas, 1, 5, &I_ButtonUp_7x4);
        canvas_draw_icon(canvas, 1, 15, &I_ButtonDown_7x4);
        canvas_draw_icon(canvas, 2, 23, &I_ButtonLeft_4x7);
        canvas_draw_icon(canvas, 2, 33, &I_ButtonRight_4x7);
        canvas_draw_icon(canvas, 0, 42, &I_Ok_btn_9x9);
        canvas_draw_icon(canvas, 0, 53, &I_back_10px);

        //Labels
        canvas_set_font(canvas, FontSecondary);

        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            8,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->up_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            18,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->down_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            28,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->left_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            38,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->right_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            48,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->ok_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            58,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->back_button));

        canvas_draw_line(canvas, 0, 65, 64, 65);

        canvas_draw_icon(canvas, 1, 70, &I_ButtonUp_7x4);
        canvas_draw_icon(canvas, 1, 80, &I_ButtonDown_7x4);
        canvas_draw_icon(canvas, 2, 88, &I_ButtonLeft_4x7);
        canvas_draw_icon(canvas, 2, 98, &I_ButtonRight_4x7);
        canvas_draw_icon(canvas, 0, 107, &I_Ok_btn_9x9);
        canvas_draw_icon(canvas, 0, 118, &I_back_10px);

        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            73,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->up_hold_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            83,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->down_hold_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            93,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->left_hold_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            103,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->right_hold_button));
        canvas_draw_str_aligned(
            canvas,
            MENU_BTN_TXT_X,
            113,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(app->ok_hold_button));
        canvas_draw_str_aligned(canvas, MENU_BTN_TXT_X, 123, AlignCenter, AlignCenter, "Exit App");
    }
}

static void app_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);

    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t infrared_remote_app(char* p) {
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // App button string
    IRApp* app = malloc(sizeof(IRApp));
    app->up_button = furi_string_alloc();
    app->down_button = furi_string_alloc();
    app->left_button = furi_string_alloc();
    app->right_button = furi_string_alloc();
    app->ok_button = furi_string_alloc();
    app->back_button = furi_string_alloc();
    app->up_hold_button = furi_string_alloc();
    app->down_hold_button = furi_string_alloc();
    app->left_hold_button = furi_string_alloc();
    app->right_hold_button = furi_string_alloc();
    app->ok_hold_button = furi_string_alloc();
    // Default repeat signal when hold
    app->repeat_signal = true;
    app->view_port = view_port_alloc();
    app->infrared_worker = infrared_worker_alloc();

    // Configure view port
    view_port_draw_callback_set(app->view_port, app_draw_callback, app);
    view_port_input_callback_set(app->view_port, app_input_callback, event_queue);

    // Register view port in GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;

    FuriString* map_file = furi_string_alloc();
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!storage_file_exists(storage, IR_REMOTE_PATH)) {
        storage_common_mkdir(storage, IR_REMOTE_PATH); //Make Folder If dir not exist
    }

    bool res;
    if(p && strlen(p)) {
        furi_string_set(map_file, p);
        res = true;
    } else {
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogsFileBrowserOptions browser_options;
        dialog_file_browser_set_basic_options(&browser_options, ".txt", &I_sub1_10px);
        browser_options.base_path = IR_REMOTE_PATH;
        furi_string_set(map_file, IR_REMOTE_PATH);
        res = dialog_file_browser_show(dialogs, map_file, map_file, &browser_options);
        furi_record_close(RECORD_DIALOGS);
    }

    // if user didn't choose anything, free everything and exit
    if(!res) {
        FURI_LOG_I(TAG, "exit");
        flipper_format_free(ff);
        furi_record_close(RECORD_STORAGE);

        furi_string_free(app->up_button);
        furi_string_free(app->down_button);
        furi_string_free(app->left_button);
        furi_string_free(app->right_button);
        furi_string_free(app->ok_button);
        furi_string_free(app->back_button);
        furi_string_free(app->up_hold_button);
        furi_string_free(app->down_hold_button);
        furi_string_free(app->left_hold_button);
        furi_string_free(app->right_hold_button);
        furi_string_free(app->ok_hold_button);

        view_port_enabled_set(app->view_port, false);
        gui_remove_view_port(gui, app->view_port);
        view_port_free(app->view_port);
        free(app);
        furi_message_queue_free(event_queue);

        furi_record_close(RECORD_GUI);
        return 255;
    }

    InfraredRemote* remote = infrared_remote_alloc();
    FuriString* remote_path = furi_string_alloc();

    InfraredSignal* up_signal = infrared_signal_alloc();
    InfraredSignal* down_signal = infrared_signal_alloc();
    InfraredSignal* left_signal = infrared_signal_alloc();
    InfraredSignal* right_signal = infrared_signal_alloc();
    InfraredSignal* ok_signal = infrared_signal_alloc();
    InfraredSignal* back_signal = infrared_signal_alloc();
    InfraredSignal* up_hold_signal = infrared_signal_alloc();
    InfraredSignal* down_hold_signal = infrared_signal_alloc();
    InfraredSignal* left_hold_signal = infrared_signal_alloc();
    InfraredSignal* right_hold_signal = infrared_signal_alloc();
    InfraredSignal* ok_hold_signal = infrared_signal_alloc();

    InfraredSignal* active_signal = NULL;
    bool is_transmitting = false;

    bool up_enabled = false;
    bool down_enabled = false;
    bool left_enabled = false;
    bool right_enabled = false;
    bool ok_enabled = false;
    bool back_enabled = false;
    bool up_hold_enabled = false;
    bool down_hold_enabled = false;
    bool left_hold_enabled = false;
    bool right_hold_enabled = false;
    bool ok_hold_enabled = false;

    if(!flipper_format_file_open_existing(ff, furi_string_get_cstr(map_file))) {
        FURI_LOG_E(TAG, "Could not open MAP file %s", furi_string_get_cstr(map_file));
        app->status = 1;
    } else {
        //Filename Assignment/Check Start

        if(!flipper_format_read_string(ff, "REMOTE", remote_path)) {
            FURI_LOG_E(TAG, "Could not read REMOTE string");
            app->status = 1;
        } else {
            if(!infrared_remote_load(remote, remote_path)) {
                FURI_LOG_E(TAG, "Could not load ir file: %s", furi_string_get_cstr(remote_path));
                app->status = 1;
            } else {
                FURI_LOG_I(TAG, "Loaded REMOTE file: %s", furi_string_get_cstr(remote_path));
            }
        }

        //assign variables to values within map file
        //set missing filenames to N/A
        //assign button signals
        size_t index = 0;
        // Add rewind to fix the string ordering issue
        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "UP", app->up_button)) {
            FURI_LOG_W(TAG, "Could not read UP string");
            furi_string_set(app->up_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->up_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                up_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                up_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "DOWN", app->down_button)) {
            FURI_LOG_W(TAG, "Could not read DOWN string");
            furi_string_set(app->down_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->down_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                down_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                down_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "LEFT", app->left_button)) {
            FURI_LOG_W(TAG, "Could not read LEFT string");
            furi_string_set(app->left_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->left_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                left_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                left_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "RIGHT", app->right_button)) {
            FURI_LOG_W(TAG, "Could not read RIGHT string");
            furi_string_set(app->right_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->right_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                right_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                right_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "OK", app->ok_button)) {
            FURI_LOG_W(TAG, "Could not read OK string");
            furi_string_set(app->ok_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->ok_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                ok_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                ok_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "BACK", app->back_button)) {
            FURI_LOG_W(TAG, "Could not read BACK string");
            furi_string_set(app->back_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->back_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                back_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                back_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "UPHOLD", app->up_hold_button)) {
            FURI_LOG_W(TAG, "Could not read UPHOLD string");
            furi_string_set(app->up_hold_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->up_hold_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                up_hold_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                up_hold_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "DOWNHOLD", app->down_hold_button)) {
            FURI_LOG_W(TAG, "Could not read DOWNHOLD string");
            furi_string_set(app->down_hold_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->down_hold_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                down_hold_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                down_hold_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "LEFTHOLD", app->left_hold_button)) {
            FURI_LOG_W(TAG, "Could not read LEFTHOLD string");
            furi_string_set(app->left_hold_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->left_hold_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                left_hold_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                left_hold_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "RIGHTHOLD", app->right_hold_button)) {
            FURI_LOG_W(TAG, "Could not read RIGHTHOLD string");
            furi_string_set(app->right_hold_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->right_hold_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                right_hold_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                right_hold_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "OKHOLD", app->ok_hold_button)) {
            FURI_LOG_W(TAG, "Could not read OKHOLD string");
            furi_string_set(app->ok_hold_button, "N/A");
        } else {
            if(!infrared_remote_find_button_by_name(
                   remote, furi_string_get_cstr(app->ok_hold_button), &index)) {
                FURI_LOG_W(TAG, "Error");
            } else {
                ok_hold_signal =
                    infrared_remote_button_get_signal(infrared_remote_get_button(remote, index));
                ok_hold_enabled = true;
            }
        }

        flipper_format_rewind(ff);
        // Find REPEATSIGNAL in the txt file, else default set to true
        if(!flipper_format_read_bool(ff, "REPEATSIGNAL", &app->repeat_signal, 1)) {
            FURI_LOG_W(TAG, "Could not read REPEATSIGNAL string");
        }

        //Debug
        if(app->repeat_signal) {
            FURI_LOG_W(TAG, "repeat");
        } else {
            FURI_LOG_W(TAG, "not repeat");
        }
    }

    furi_string_free(remote_path);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);

    bool otg_was_enabled = furi_hal_power_is_otg_enabled();
    InfraredSettings settings = {0};
    saved_struct_load(
        INFRARED_SETTINGS_PATH,
        &settings,
        sizeof(InfraredSettings),
        INFRARED_SETTINGS_MAGIC,
        INFRARED_SETTINGS_VERSION);
    if(settings.tx_pin < FuriHalInfraredTxPinMax) {
        furi_hal_infrared_set_tx_output(settings.tx_pin);
        if(settings.otg_enabled != otg_was_enabled) {
            if(settings.otg_enabled) {
                furi_hal_power_enable_otg();
            } else {
                furi_hal_power_disable_otg();
            }
        }
    } else {
        FuriHalInfraredTxPin tx_pin_detected = furi_hal_infrared_detect_tx_output();
        furi_hal_infrared_set_tx_output(tx_pin_detected);
        if(tx_pin_detected != FuriHalInfraredTxPinInternal) {
            furi_hal_power_enable_otg();
        }
    }

    bool running = true;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);

    if(app->status) {
        view_port_update(app->view_port);
        while(running) {
            if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
                if(event.type == InputTypeShort) {
                    switch(event.key) {
                    case InputKeyBack:
                        running = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    } else {
        view_port_update(app->view_port);
        if(app->repeat_signal) {
            // Repeat signal when hold
            while(running) {
                if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
                    // short press signal
                    if(event.type == InputTypeShort) {
                        switch(event.key) {
                        case InputKeyUp:
                            if(up_enabled) {
                                active_signal = up_signal;
                                FURI_LOG_I(TAG, "up");
                            }
                            break;
                        case InputKeyDown:
                            if(down_enabled) {
                                active_signal = down_signal;
                                FURI_LOG_I(TAG, "down");
                            }
                            break;
                        case InputKeyRight:
                            if(right_enabled) {
                                active_signal = right_signal;
                                FURI_LOG_I(TAG, "right");
                            }
                            break;
                        case InputKeyLeft:
                            if(left_enabled) {
                                active_signal = left_signal;
                                FURI_LOG_I(TAG, "left");
                            }
                            break;
                        case InputKeyOk:
                            if(ok_enabled) {
                                active_signal = ok_signal;
                                FURI_LOG_I(TAG, "ok");
                            }
                            break;
                        case InputKeyBack:
                            if(back_enabled) {
                                active_signal = back_signal;
                                FURI_LOG_I(TAG, "back");
                            }
                            break;
                        default:
                            running = false;
                            break;
                        }
                        // long press signal
                    } else if(event.type == InputTypeLong) {
                        switch(event.key) {
                        case InputKeyUp:
                            if(up_hold_enabled) {
                                active_signal = up_hold_signal;
                                FURI_LOG_I(TAG, "up!");
                            }
                            break;
                        case InputKeyDown:
                            if(down_hold_enabled) {
                                active_signal = down_hold_signal;
                                FURI_LOG_I(TAG, "down!");
                            }
                            break;
                        case InputKeyRight:
                            if(right_hold_enabled) {
                                active_signal = right_hold_signal;
                                FURI_LOG_I(TAG, "right!");
                            }
                            break;
                        case InputKeyLeft:
                            if(left_hold_enabled) {
                                active_signal = left_hold_signal;
                                FURI_LOG_I(TAG, "left!");
                            }
                            break;
                        case InputKeyOk:
                            if(ok_hold_enabled) {
                                active_signal = ok_hold_signal;
                                FURI_LOG_I(TAG, "ok!");
                            }
                            break;
                        default:
                            running = false;
                            break;
                        }
                    } else if(event.type == InputTypeRelease && is_transmitting) {
                        notification_message(notification, &sequence_blink_stop);
                        infrared_worker_tx_stop(app->infrared_worker);
                        is_transmitting = false;
                        active_signal = NULL;
                    }
                    if(active_signal != NULL &&
                       (event.type == InputTypeShort || event.type == InputTypeLong)) {
                        if(is_transmitting) {
                            infrared_worker_tx_stop(app->infrared_worker);
                        }
                        // Check the signal is raw
                        if(active_signal->is_raw) {
                            InfraredRawSignal* raw_signal =
                                infrared_signal_get_raw_signal(active_signal);
                            infrared_worker_set_raw_signal(
                                app->infrared_worker,
                                raw_signal->timings,
                                raw_signal->timings_size,
                                raw_signal->frequency,
                                raw_signal->duty_cycle);
                        } else {
                            InfraredMessage* message = infrared_signal_get_message(active_signal);
                            infrared_worker_set_decoded_signal(app->infrared_worker, message);
                        }

                        infrared_worker_tx_set_get_signal_callback(
                            app->infrared_worker,
                            infrared_worker_tx_get_signal_steady_callback,
                            app);

                        infrared_worker_tx_start(app->infrared_worker);
                        notification_message(notification, &sequence_blink_start_magenta);
                        is_transmitting = true;
                    }
                }
            }
        } else {
            // Not repeat signal when hold
            while(running) {
                if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
                    // short press signal
                    if(event.type == InputTypeShort) {
                        switch(event.key) {
                        case InputKeyUp:
                            if(up_enabled) {
                                infrared_signal_transmit(up_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "up");
                            }
                            break;
                        case InputKeyDown:
                            if(down_enabled) {
                                infrared_signal_transmit(down_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "down");
                            }
                            break;
                        case InputKeyRight:
                            if(right_enabled) {
                                infrared_signal_transmit(right_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "right");
                            }
                            break;
                        case InputKeyLeft:
                            if(left_enabled) {
                                infrared_signal_transmit(left_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "left");
                            }
                            break;
                        case InputKeyOk:
                            if(ok_enabled) {
                                infrared_signal_transmit(ok_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "ok");
                            }
                            break;
                        case InputKeyBack:
                            if(back_enabled) {
                                infrared_signal_transmit(back_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "back");
                            }
                            break;
                        default:
                            running = false;
                            break;
                        }
                        // long press signal
                    } else if(event.type == InputTypeLong) {
                        switch(event.key) {
                        case InputKeyUp:
                            if(up_hold_enabled) {
                                infrared_signal_transmit(up_hold_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "up!");
                            }
                            break;
                        case InputKeyDown:
                            if(down_hold_enabled) {
                                infrared_signal_transmit(down_hold_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "down!");
                            }
                            break;
                        case InputKeyRight:
                            if(right_hold_enabled) {
                                infrared_signal_transmit(right_hold_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "right!");
                            }
                            break;
                        case InputKeyLeft:
                            if(left_hold_enabled) {
                                infrared_signal_transmit(left_hold_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "left!");
                            }
                            break;
                        case InputKeyOk:
                            if(ok_hold_enabled) {
                                infrared_signal_transmit(ok_hold_signal);
                                notification_message(notification, &sequence_blink_start_magenta);
                                FURI_LOG_I(TAG, "ok!");
                            }
                            break;
                        default:
                            running = false;
                            break;
                        }
                    } else if(event.type == InputTypeRelease) {
                        notification_message(notification, &sequence_blink_stop);
                    }
                }
            }
        }
    }

    furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);
    if(furi_hal_power_is_otg_enabled() != otg_was_enabled) {
        if(otg_was_enabled) {
            furi_hal_power_enable_otg();
        } else {
            furi_hal_power_disable_otg();
        }
    }

    // Free all things
    furi_string_free(app->up_button);
    furi_string_free(app->down_button);
    furi_string_free(app->left_button);
    furi_string_free(app->right_button);
    furi_string_free(app->ok_button);
    furi_string_free(app->back_button);
    furi_string_free(app->up_hold_button);
    furi_string_free(app->down_hold_button);
    furi_string_free(app->left_hold_button);
    furi_string_free(app->right_hold_button);
    furi_string_free(app->ok_hold_button);

    if(is_transmitting) {
        infrared_worker_tx_stop(app->infrared_worker);
        notification_message(notification, &sequence_blink_stop);
    }
    infrared_worker_free(app->infrared_worker);

    infrared_remote_free(remote);
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(gui, app->view_port);
    view_port_free(app->view_port);
    free(app);
    furi_message_queue_free(event_queue);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    return 0;
}