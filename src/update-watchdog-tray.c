#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_INTERVAL_SECONDS 3600

static AppIndicator *indicator;
static GtkWidget *cc_item;
static GtkWidget *codex_item;
static GtkWidget *checked_item;
static GtkWidget *check_item;
static gboolean check_running = FALSE;
static gchar *codex_update_command = NULL;

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
    gchar *codex_label = g_strdup_printf("Codex pre-release: %s  ->  %s", codex_installed, codex_available);
    gchar *checked_label = g_strdup_printf("上次检查: %s", checked_at);
    gtk_menu_item_set_label(GTK_MENU_ITEM(cc_item), cc_label);
    gtk_menu_item_set_label(GTK_MENU_ITEM(codex_item), codex_label);
    gtk_menu_item_set_label(GTK_MENU_ITEM(checked_item), checked_label);
    g_free(codex_update_command);
    codex_update_command = g_strdup_printf("pnpm install -g @openai/codex@%s", codex_available);

    gboolean has_update =
        version_is_newer(cc_available, cc_installed) ||
        version_is_newer(codex_available, codex_installed);
    app_indicator_set_icon_full(indicator,
        has_update ? "software-updates-updates" : "software-updates-inactive",
        has_update ? "有可用更新" : "软件均为最新版本");
    app_indicator_set_attention_icon_full(indicator, "software-updates-updates", "有可用更新");
    app_indicator_set_status(indicator,
        has_update ? APP_INDICATOR_STATUS_ATTENTION : APP_INDICATOR_STATUS_ACTIVE);

    g_free(cc_label);
    g_free(codex_label);
    g_free(checked_label);
    g_free(cc_installed);
    g_free(cc_available);
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

static void copy_command(GtkMenuItem *item, gpointer command) {
    copy_with_feedback(item, command);
}

static void copy_codex_command(GtkMenuItem *item, gpointer data) {
    (void)data;
    if (codex_update_command != NULL) {
        copy_with_feedback(item, codex_update_command);
    }
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
    codex_item = disabled_item("Codex pre-release: 读取中...");
    checked_item = disabled_item("上次检查: 尚未检查");
    check_item = gtk_menu_item_new_with_label("立即检查");
    GtkWidget *copy_cc = gtk_menu_item_new_with_label("复制 CC Switch 更新命令");
    GtkWidget *copy_codex = gtk_menu_item_new_with_label("复制 Codex pre-release 更新命令");
    GtkWidget *quit_item = gtk_menu_item_new_with_label("退出");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), title);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), cc_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), codex_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), checked_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), check_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_cc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_codex);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    g_signal_connect(check_item, "activate", G_CALLBACK(start_check), NULL);
    g_object_set_data_full(G_OBJECT(copy_cc), "original-label",
                           g_strdup("复制 CC Switch 更新命令"), g_free);
    g_object_set_data_full(G_OBJECT(copy_codex), "original-label",
                           g_strdup("复制 Codex pre-release 更新命令"), g_free);
    g_signal_connect(copy_cc, "activate", G_CALLBACK(copy_command), "yay -Syu cc-switch-bin");
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
