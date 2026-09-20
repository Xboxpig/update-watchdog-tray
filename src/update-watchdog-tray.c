#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define CHECK_INTERVAL_SECONDS 3600

static AppIndicator *indicator;
static GtkWidget *cc_item;
static GtkWidget *chatgpt_item;
static GtkWidget *codex_item;
static GtkWidget *checked_item;
static GtkWidget *check_item;
static GtkWidget *update_cc_item;
static GtkWidget *update_chatgpt_item;
static gboolean check_running = FALSE;
static gboolean update_running = FALSE;
static gchar *codex_update_command = NULL;

typedef enum {
    RESTART_CC_SWITCH,
    RESTART_CHATGPT
} UpgradeRestartTarget;

typedef struct {
    GtkWidget *dialog;
    GtkWidget *stage_label;
    GtkWidget *detail_label;
    GtkWidget *progress_bar;
    GtkWidget *log_view;
    GtkWidget *cancel_button;
    GtkWidget *close_button;
    GtkWidget *restart_button;
    GSubprocess *process;
    GDataInputStream *output;
    gboolean cancelable;
    gboolean terminal_event;
    gboolean helper_finished;
    gboolean update_succeeded;
    gchar *warning_detail;
    gchar *display_name;
    UpgradeRestartTarget restart_target;
} UpgradeDialog;

static const char *home_dir(void) {
    return g_get_home_dir();
}

static char *status_path(void) {
    return g_build_filename(home_dir(), ".local", "state", "update-watchdog", "status", NULL);
}

static gboolean version_is_newer(const char *available, const char *installed) {
    if (g_strcmp0(available, "unknown") == 0 || g_strcmp0(installed, "unknown") == 0) {
        return FALSE;
    }

    gchar *argv[] = {"vercmp", (gchar *)available, (gchar *)installed, NULL};
    gchar *output = NULL;
    gint exit_status = 1;

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                      &output, NULL, &exit_status, NULL)) {
        return FALSE;
    }

    gboolean newer = exit_status == 0 && atoi(output) > 0;
    g_free(output);
    return newer;
}

static void set_status_labels(void) {
    gchar *path = status_path();
    gchar *contents = NULL;
    gchar *cc_installed = g_strdup("unknown");
    gchar *cc_available = g_strdup("unknown");
    gchar *chatgpt_installed = g_strdup("unknown");
    gchar *chatgpt_available = g_strdup("unknown");
    gchar *codex_installed = g_strdup("unknown");
    gchar *codex_available = g_strdup("unknown");
    gchar *checked_at = g_strdup("尚未检查");

    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            gchar **fields = g_strsplit(lines[i], "\t", 3);
            if (g_strcmp0(fields[0], "cc_switch") == 0 && fields[1] && fields[2]) {
                g_free(cc_installed);
                g_free(cc_available);
                cc_installed = g_strdup(fields[1]);
                cc_available = g_strdup(fields[2]);
            } else if (g_strcmp0(fields[0], "chatgpt_desktop") == 0 && fields[1] && fields[2]) {
                g_free(chatgpt_installed);
                g_free(chatgpt_available);
                chatgpt_installed = g_strdup(fields[1]);
                chatgpt_available = g_strdup(fields[2]);
            } else if (g_strcmp0(fields[0], "codex_prerelease") == 0 && fields[1] && fields[2]) {
                g_free(codex_installed);
                g_free(codex_available);
                codex_installed = g_strdup(fields[1]);
                codex_available = g_strdup(fields[2]);
            } else if (g_strcmp0(fields[0], "checked_at") == 0 && fields[1]) {
                g_free(checked_at);
                checked_at = g_strdup(fields[1]);
            }
            g_strfreev(fields);
        }
        g_strfreev(lines);
    }

    gchar *cc_label = g_strdup_printf("CC Switch: %s  ->  %s", cc_installed, cc_available);
    gchar *chatgpt_label = g_strdup_printf("ChatGPT Desktop: %s  ->  %s",
                                           chatgpt_installed, chatgpt_available);
    gchar *codex_label = g_strdup_printf("Codex pre-release: %s  ->  %s", codex_installed, codex_available);
    gchar *checked_label = g_strdup_printf("上次检查: %s", checked_at);
    gtk_menu_item_set_label(GTK_MENU_ITEM(cc_item), cc_label);
    gtk_menu_item_set_label(GTK_MENU_ITEM(chatgpt_item), chatgpt_label);
    gtk_menu_item_set_label(GTK_MENU_ITEM(codex_item), codex_label);
    gtk_menu_item_set_label(GTK_MENU_ITEM(checked_item), checked_label);
    g_free(codex_update_command);
    codex_update_command = g_strdup_printf("pnpm install -g @openai/codex@%s", codex_available);

    gboolean has_update =
        version_is_newer(cc_available, cc_installed) ||
        version_is_newer(chatgpt_available, chatgpt_installed) ||
        version_is_newer(codex_available, codex_installed);
    app_indicator_set_icon_full(indicator,
        has_update ? "software-updates-updates" : "software-updates-inactive",
        has_update ? "有可用更新" : "软件均为最新版本");
    app_indicator_set_attention_icon_full(indicator, "software-updates-updates", "有可用更新");
    app_indicator_set_status(indicator,
        has_update ? APP_INDICATOR_STATUS_ATTENTION : APP_INDICATOR_STATUS_ACTIVE);

    g_free(cc_label);
    g_free(chatgpt_label);
    g_free(codex_label);
    g_free(checked_label);
    g_free(cc_installed);
    g_free(cc_available);
    g_free(chatgpt_installed);
    g_free(chatgpt_available);
    g_free(codex_installed);
    g_free(codex_available);
    g_free(checked_at);
    g_free(contents);
    g_free(path);
}

static void check_finished(GObject *source, GAsyncResult *result, gpointer data) {
    (void)data;
    GSubprocess *process = G_SUBPROCESS(source);
    GError *error = NULL;
    g_subprocess_wait_finish(process, result, &error);
    if (error != NULL) {
        g_warning("Update check failed: %s", error->message);
        g_error_free(error);
    }
    check_running = FALSE;
    gtk_widget_set_sensitive(check_item, TRUE);
    gtk_menu_item_set_label(GTK_MENU_ITEM(check_item), "立即检查");
    set_status_labels();
}

static void start_check(GtkMenuItem *item, gpointer data) {
    (void)item;
    (void)data;
    if (check_running) {
        return;
    }

    gchar *script = g_build_filename(home_dir(), ".local", "bin", "update-watchdog", NULL);
    GError *error = NULL;
    GSubprocess *process = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, script, NULL);
    g_free(script);

    if (process == NULL) {
        g_warning("Could not start update check: %s", error->message);
        g_error_free(error);
        return;
    }

    check_running = TRUE;
    gtk_widget_set_sensitive(check_item, FALSE);
    gtk_menu_item_set_label(GTK_MENU_ITEM(check_item), "正在检查...");
    g_subprocess_wait_async(process, NULL, check_finished, NULL);
    g_object_unref(process);
}

static gboolean hourly_check(gpointer data) {
    (void)data;
    start_check(NULL, NULL);
    return G_SOURCE_CONTINUE;
}

static gboolean set_clipboard_text(const gchar *text) {
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (bus != NULL) {
        GVariant *result = g_dbus_connection_call_sync(
            bus,
            "org.kde.klipper",
            "/klipper",
            "org.kde.klipper.klipper",
            "setClipboardContents",
            g_variant_new("(s)", text),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            3000,
            NULL,
            &error);
        g_object_unref(bus);
        if (result != NULL) {
            g_variant_unref(result);
            return TRUE;
        }
    }

    if (error != NULL) {
        g_warning("Could not write KDE clipboard: %s", error->message);
        g_error_free(error);
    }

    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    gtk_clipboard_store(clipboard);
    return FALSE;
}

static gboolean reset_copy_label(gpointer item) {
    const gchar *original = g_object_get_data(G_OBJECT(item), "original-label");
    gtk_menu_item_set_label(GTK_MENU_ITEM(item), original);
    return G_SOURCE_REMOVE;
}

static void copy_with_feedback(GtkMenuItem *item, const gchar *command) {
    gboolean kde_success = set_clipboard_text(command);
    gtk_menu_item_set_label(item, kde_success ? "已复制到 KDE 剪贴板" : "已请求复制");
    g_timeout_add_seconds(2, reset_copy_label, item);
}

static void copy_codex_command(GtkMenuItem *item, gpointer data) {
    (void)data;
    if (codex_update_command != NULL) {
        copy_with_feedback(item, codex_update_command);
    }
}

static void upgrade_append_log(UpgradeDialog *upgrade, const gchar *level,
                               const gchar *message) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(upgrade->log_view));
    GtkTextIter end;
    gchar *line = g_strdup_printf("[%s] %s\n", level, message);

    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, line, -1);
    g_free(line);
}

static void upgrade_set_state(UpgradeDialog *upgrade, const gchar *stage,
                              const gchar *detail, gint percent,
                              gboolean cancelable) {
    gchar *stage_markup = g_markup_printf_escaped("<b>%s</b>", stage);

    gtk_label_set_markup(GTK_LABEL(upgrade->stage_label), stage_markup);
    gtk_label_set_text(GTK_LABEL(upgrade->detail_label), detail);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(upgrade->progress_bar),
                                  CLAMP(percent, 0, 100) / 100.0);
    gchar *progress_text = g_strdup_printf("%d%%", CLAMP(percent, 0, 100));
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(upgrade->progress_bar), progress_text);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(upgrade->progress_bar), TRUE);
    upgrade->cancelable = cancelable;
    gtk_widget_set_sensitive(upgrade->cancel_button, cancelable && !upgrade->terminal_event);

    g_free(stage_markup);
    g_free(progress_text);
}

static void upgrade_show_terminal_controls(UpgradeDialog *upgrade) {
    upgrade->terminal_event = TRUE;
    update_running = FALSE;
    gtk_window_set_deletable(GTK_WINDOW(upgrade->dialog), TRUE);
    gtk_widget_hide(upgrade->cancel_button);
    gtk_widget_set_sensitive(update_cc_item, TRUE);
    gtk_widget_set_sensitive(update_chatgpt_item, TRUE);

    if (upgrade->update_succeeded && upgrade->restart_button == NULL) {
        gchar *restart_label = g_strdup_printf("重启 %s", upgrade->display_name);
        upgrade->restart_button = gtk_dialog_add_button(GTK_DIALOG(upgrade->dialog),
                                                        restart_label, GTK_RESPONSE_APPLY);
        gtk_widget_show(upgrade->restart_button);
        g_free(restart_label);
    }
    if (upgrade->close_button == NULL) {
        upgrade->close_button = gtk_dialog_add_button(GTK_DIALOG(upgrade->dialog),
                                                      "完成", GTK_RESPONSE_CLOSE);
        gtk_widget_show(upgrade->close_button);
    }
}

static gboolean launch_updated_application(gpointer data) {
    UpgradeRestartTarget target = GPOINTER_TO_INT(data);
    gchar *chatgpt_wrapper = NULL;
    gchar *executable = NULL;
    GError *error = NULL;

    if (target == RESTART_CC_SWITCH) {
        executable = g_strdup("/usr/bin/cc-switch");
    } else {
        chatgpt_wrapper = g_build_filename(home_dir(), ".local", "bin",
                                           "chatgpt-no-tray", NULL);
        executable = g_strdup(g_file_test(chatgpt_wrapper, G_FILE_TEST_IS_EXECUTABLE) ?
                              chatgpt_wrapper : "/usr/bin/chatgpt");
    }
    gchar *argv[] = {executable, NULL};
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                       NULL, &error)) {
        g_warning("Could not restart updated application: %s", error->message);
        g_error_free(error);
    }
    g_free(executable);
    g_free(chatgpt_wrapper);
    return G_SOURCE_REMOVE;
}

static void restart_updated_application(UpgradeDialog *upgrade) {
    const gchar *process_name = upgrade->restart_target == RESTART_CC_SWITCH ?
                                "cc-switch" : "ChatGPT";
    gchar *argv[] = {"pkill", "-x", (gchar *)process_name, NULL};
    GError *error = NULL;
    gchar *detail = g_strdup_printf("正在重新启动 %s 以载入已安装版本。",
                                    upgrade->display_name);

    gtk_widget_set_sensitive(upgrade->restart_button, FALSE);
    gtk_label_set_text(GTK_LABEL(upgrade->detail_label), detail);
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                       NULL, &error)) {
        g_warning("Could not stop %s: %s", upgrade->display_name, error->message);
        g_error_free(error);
    }
    g_timeout_add(400, launch_updated_application,
                  GINT_TO_POINTER(upgrade->restart_target));
    g_free(detail);
}

static void cancel_upgrade_process(UpgradeDialog *upgrade) {
    const gchar *identifier = g_subprocess_get_identifier(upgrade->process);
    gchar *end = NULL;
    gint64 pid = identifier == NULL ? 0 : g_ascii_strtoll(identifier, &end, 10);

    if (end != identifier && end != NULL && *end == '\0' && pid > 0 &&
        kill((pid_t)-pid, SIGTERM) == 0) {
        return;
    }
    g_subprocess_send_signal(upgrade->process, SIGTERM);
}

static gboolean upgrade_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)widget;
    (void)event;
    UpgradeDialog *upgrade = data;
    return !upgrade->terminal_event;
}

static void upgrade_dialog_response(GtkDialog *dialog, gint response, gpointer data) {
    UpgradeDialog *upgrade = data;

    if (response == GTK_RESPONSE_CANCEL) {
        if (upgrade->cancelable && upgrade->process != NULL && !upgrade->terminal_event) {
            gtk_widget_set_sensitive(upgrade->cancel_button, FALSE);
            gtk_label_set_text(GTK_LABEL(upgrade->detail_label), "正在取消可中断阶段...");
            cancel_upgrade_process(upgrade);
        }
    } else if (response == GTK_RESPONSE_APPLY) {
        restart_updated_application(upgrade);
    } else if (response == GTK_RESPONSE_CLOSE && upgrade->terminal_event) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void upgrade_complete_with_protocol_error(UpgradeDialog *upgrade, const gchar *detail) {
    upgrade->update_succeeded = FALSE;
    upgrade_set_state(upgrade, "升级器协议错误", detail, 100, FALSE);
    upgrade_append_log(upgrade, "E_PROTOCOL", detail);
}

static void upgrade_wait_finished(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    UpgradeDialog *upgrade = data;
    GError *error = NULL;

    if (!g_subprocess_wait_check_finish(upgrade->process, result, &error)) {
        if (!upgrade->terminal_event) {
            gchar *detail = g_strdup_printf("升级器异常退出：%s", error->message);
            upgrade_complete_with_protocol_error(upgrade, detail);
            g_free(detail);
        }
        g_error_free(error);
    } else if (!upgrade->terminal_event) {
        upgrade_complete_with_protocol_error(upgrade, "升级器未返回最终状态事件。");
    }
    upgrade->helper_finished = TRUE;
    upgrade_show_terminal_controls(upgrade);
}

static void upgrade_handle_event(UpgradeDialog *upgrade, const gchar *line) {
    gchar **fields = g_strsplit(line, "\t", 5);

    if (fields[0] == NULL || fields[0][0] == '\0') {
        g_strfreev(fields);
        return;
    }

    if (g_strcmp0(fields[0], "PROGRESS") == 0 && fields[1] && fields[2] && fields[3] && fields[4]) {
        gchar *end = NULL;
        gint64 percent = g_ascii_strtoll(fields[1], &end, 10);
        if (end != fields[1] && *end == '\0' && percent >= 0 && percent <= 100) {
            gboolean cancelable = g_strcmp0(fields[4], "true") == 0;
            upgrade_set_state(upgrade, fields[2], fields[3], (gint)percent, cancelable);
            upgrade_append_log(upgrade, "阶段", fields[3]);
        } else {
            upgrade_append_log(upgrade, "E_PROTOCOL", "收到无效的进度百分比。");
        }
    } else if (g_strcmp0(fields[0], "LOG") == 0 && fields[1] && fields[2]) {
        upgrade_append_log(upgrade, fields[1], fields[2]);
    } else if (g_strcmp0(fields[0], "WARNING") == 0 && fields[1] && fields[2] && fields[3]) {
        gchar *detail = g_strdup_printf("%s\n%s", fields[2], fields[3]);
        g_free(upgrade->warning_detail);
        upgrade->warning_detail = g_strdup_printf("%s：%s", fields[2], fields[3]);
        gtk_label_set_text(GTK_LABEL(upgrade->detail_label), detail);
        upgrade_append_log(upgrade, fields[1], detail);
        g_free(detail);
    } else if (g_strcmp0(fields[0], "ERROR") == 0 && fields[1] && fields[2] && fields[3]) {
        gchar *stage = g_strdup_printf("更新失败 (%s)", fields[1]);
        gchar *detail = g_strdup_printf("%s\n%s", fields[2], fields[3]);
        upgrade->terminal_event = TRUE;
        upgrade->update_succeeded = FALSE;
        upgrade_set_state(upgrade, stage, detail, 100, FALSE);
        upgrade_append_log(upgrade, fields[1], detail);
        g_free(stage);
        g_free(detail);
    } else if (g_strcmp0(fields[0], "RESULT") == 0 && fields[1] && fields[2] && fields[3]) {
        gchar *stage = NULL;
        gchar *detail = NULL;
        if (g_strcmp0(fields[1], "success") == 0) {
            stage = g_strdup_printf("%s %s 已安装", upgrade->display_name, fields[2]);
            upgrade->update_succeeded = TRUE;
        } else {
            stage = g_strdup("无需更新");
            upgrade->update_succeeded = FALSE;
        }
        upgrade->terminal_event = TRUE;
        detail = upgrade->warning_detail == NULL ? g_strdup(fields[3]) :
            g_strdup_printf("%s\n\n注意：%s", fields[3], upgrade->warning_detail);
        upgrade_set_state(upgrade, stage, detail, 100, FALSE);
        upgrade_append_log(upgrade, "结果", detail);
        g_free(stage);
        g_free(detail);
    } else {
        upgrade_append_log(upgrade, "输出", line);
    }
    g_strfreev(fields);
}

static void upgrade_read_next_line(UpgradeDialog *upgrade);

static void upgrade_line_ready(GObject *source, GAsyncResult *result, gpointer data) {
    UpgradeDialog *upgrade = data;
    GError *error = NULL;
    gsize length = 0;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result,
                                                        &length, &error);

    if (line != NULL) {
        upgrade_handle_event(upgrade, line);
        g_free(line);
        upgrade_read_next_line(upgrade);
        return;
    }
    if (error != NULL) {
        if (!upgrade->terminal_event) {
            gchar *detail = g_strdup_printf("无法读取升级器输出：%s", error->message);
            upgrade_complete_with_protocol_error(upgrade, detail);
            g_free(detail);
        }
        g_error_free(error);
    }
    g_subprocess_wait_check_async(upgrade->process, NULL, upgrade_wait_finished, upgrade);
}

static void upgrade_read_next_line(UpgradeDialog *upgrade) {
    g_data_input_stream_read_line_async(upgrade->output, G_PRIORITY_DEFAULT, NULL,
                                        upgrade_line_ready, upgrade);
}

static void upgrade_dialog_free(gpointer data) {
    UpgradeDialog *upgrade = data;
    g_clear_object(&upgrade->output);
    g_clear_object(&upgrade->process);
    g_free(upgrade->warning_detail);
    g_free(upgrade->display_name);
    g_free(upgrade);
}

static void start_upgrade(const gchar *display_name, const gchar *script_name,
                          UpgradeRestartTarget restart_target) {
    if (update_running) {
        return;
    }

    UpgradeDialog *upgrade = g_new0(UpgradeDialog, 1);
    GtkWidget *content;
    GtkWidget *box;
    GtkWidget *expander;
    GtkWidget *scrolled;
    GError *error = NULL;
    gchar *script = g_build_filename(home_dir(), ".local", "bin", script_name, NULL);
    gchar *dialog_title = g_strdup_printf("更新 %s", display_name);

    upgrade->display_name = g_strdup(display_name);
    upgrade->restart_target = restart_target;
    upgrade->dialog = gtk_dialog_new_with_buttons(dialog_title, NULL,
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "取消", GTK_RESPONSE_CANCEL, NULL);
    g_free(dialog_title);
    gtk_window_set_default_size(GTK_WINDOW(upgrade->dialog), 520, 280);
    gtk_window_set_resizable(GTK_WINDOW(upgrade->dialog), TRUE);
    gtk_window_set_deletable(GTK_WINDOW(upgrade->dialog), FALSE);
    content = gtk_dialog_get_content_area(GTK_DIALOG(upgrade->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 18);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    upgrade->stage_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(upgrade->stage_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), upgrade->stage_label, FALSE, FALSE, 0);

    upgrade->detail_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(upgrade->detail_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(upgrade->detail_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), upgrade->detail_label, FALSE, FALSE, 0);

    upgrade->progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(box), upgrade->progress_bar, FALSE, FALSE, 0);

    expander = gtk_expander_new("诊断详情");
    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled, -1, 140);
    upgrade->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(upgrade->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(upgrade->log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(upgrade->log_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled), upgrade->log_view);
    gtk_container_add(GTK_CONTAINER(expander), scrolled);
    gtk_box_pack_start(GTK_BOX(box), expander, TRUE, TRUE, 0);

    upgrade->cancel_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(upgrade->dialog),
                                                                 GTK_RESPONSE_CANCEL);
    g_object_set_data_full(G_OBJECT(upgrade->dialog), "upgrade-dialog", upgrade,
                           upgrade_dialog_free);
    g_signal_connect(upgrade->dialog, "delete-event", G_CALLBACK(upgrade_delete_event), upgrade);
    g_signal_connect(upgrade->dialog, "response", G_CALLBACK(upgrade_dialog_response), upgrade);
    upgrade_set_state(upgrade, "正在启动升级器", "准备验证官方发布信息。", 0, TRUE);
    gtk_widget_show_all(upgrade->dialog);

    upgrade->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                         G_SUBPROCESS_FLAGS_STDERR_MERGE,
                                         &error, "/usr/bin/setsid", script, NULL);
    g_free(script);
    if (upgrade->process == NULL) {
        gchar *detail = g_strdup_printf("无法启动本地升级器：%s", error->message);
        upgrade_complete_with_protocol_error(upgrade, detail);
        upgrade->helper_finished = TRUE;
        upgrade_show_terminal_controls(upgrade);
        g_free(detail);
        g_error_free(error);
        return;
    }

    update_running = TRUE;
    gtk_widget_set_sensitive(update_cc_item, FALSE);
    gtk_widget_set_sensitive(update_chatgpt_item, FALSE);
    upgrade->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(upgrade->process));
    upgrade_read_next_line(upgrade);
}

static void start_cc_upgrade(GtkMenuItem *item, gpointer data) {
    (void)item;
    (void)data;
    start_upgrade("CC Switch", "update-cc-switch", RESTART_CC_SWITCH);
}

static void start_chatgpt_upgrade(GtkMenuItem *item, gpointer data) {
    (void)item;
    (void)data;
    start_upgrade("ChatGPT Desktop", "update-chatgpt-desktop", RESTART_CHATGPT);
}

static GtkWidget *disabled_item(const char *label) {
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_widget_set_sensitive(item, FALSE);
    return item;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    indicator = app_indicator_new(
        "update-watchdog", "software-updates-inactive", APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
    G_GNUC_END_IGNORE_DEPRECATIONS
    app_indicator_set_title(indicator, "Update Watchdog");
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget *menu = gtk_menu_new();
    GtkWidget *title = disabled_item("Update Watchdog");
    cc_item = disabled_item("CC Switch: 读取中...");
    chatgpt_item = disabled_item("ChatGPT Desktop: 读取中...");
    codex_item = disabled_item("Codex pre-release: 读取中...");
    checked_item = disabled_item("上次检查: 尚未检查");
    check_item = gtk_menu_item_new_with_label("立即检查");
    update_cc_item = gtk_menu_item_new_with_label("更新 CC Switch...");
    update_chatgpt_item = gtk_menu_item_new_with_label("更新 ChatGPT Desktop...");
    GtkWidget *copy_codex = gtk_menu_item_new_with_label("复制 Codex pre-release 更新命令");
    GtkWidget *quit_item = gtk_menu_item_new_with_label("退出");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), title);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), cc_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), chatgpt_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), codex_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), checked_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), check_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), update_cc_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), update_chatgpt_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_codex);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    g_signal_connect(check_item, "activate", G_CALLBACK(start_check), NULL);
    g_object_set_data_full(G_OBJECT(copy_codex), "original-label",
                           g_strdup("复制 Codex pre-release 更新命令"), g_free);
    g_signal_connect(update_cc_item, "activate", G_CALLBACK(start_cc_upgrade), NULL);
    g_signal_connect(update_chatgpt_item, "activate", G_CALLBACK(start_chatgpt_upgrade), NULL);
    g_signal_connect(copy_codex, "activate", G_CALLBACK(copy_codex_command), NULL);
    g_signal_connect_swapped(quit_item, "activate", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));
    set_status_labels();
    start_check(NULL, NULL);
    g_timeout_add_seconds(CHECK_INTERVAL_SECONDS, hourly_check, NULL);

    gtk_main();
    g_free(codex_update_command);
    return 0;
}
