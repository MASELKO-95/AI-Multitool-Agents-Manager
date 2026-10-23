#define _POSIX_C_SOURCE 200809L
#include "aimt/app.h"
#include "aimt/plugin.h"
#include "aimt/discovery.h"
#include "aimt/project.h"
#include "aimt/project_book.h"
#include "aimt/policy.h"
#include "aimt/catalog.h"
#include "aimt/ollama_profile.h"
#include "aimt/ollama_pull.h"
#include "aimt/local_generate.h"
#include "aimt/codex.h"
#include <math.h>

#include <SDL.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Nuklear allows a custom formatter. Round bounded numeric settings with libc
 * instead of truncating the decimal representation of a binary float. */
#define NK_MAX_NUMBER_BUFFER 64
static char *aimt_decimal_to_text(char *out, double value)
{
    snprintf(out, NK_MAX_NUMBER_BUFFER, "%.2f", value);
    return out;
}
#define NK_DTOA aimt_decimal_to_text
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INPUT_MAX 256
#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear_sdl_renderer.h"
#include "nuklear_tooltip.h"

#ifndef AIMT_DEFAULT_PLUGINS
#define AIMT_DEFAULT_PLUGINS "Modules"
#endif

typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t finished;
    aimt_discovery result;
    bool creating_profile, pulling_model, profile_success;
    SDL_atomic_t cancel_pull, pull_percent;
    char pull_model[128];
    aimt_ollama_profile profile;
    char module_id[64], notice[512];
    char ollama[256];
    char llama[256];
} discovery_job;

typedef struct {
    aimt_app *app;
    aimt_plugin_registry registry;
    aimt_plugin editing;
    int plugin;
    int settings_tab;
    int workspace_tab;
    bool show_settings;
    bool reset_settings_scroll;
    aimt_catalog catalog;
    aimt_task task;
    const char *settings_path;
    bool edit_details;
    size_t saved_settings;
    size_t pending_download;
    float font_scale;
    struct nk_font *code_font;
    aimt_policy policy;
    int cloud_provider;
    aimt_project project;
    aimt_project_book projects;
    const char *projects_path;
    char project_input[4096];
    bool show_project_picker;
    bool creating_project;
    aimt_project picker;
    char picker_input[4096];
    char new_folder[256];
    aimt_project_entry picker_entries[AIMT_PROJECT_ENTRIES_MAX];
    size_t picker_count;
    bool picker_truncated;
    pid_t terminal_pid;
    char relative[4096];
    aimt_project_entry entries[AIMT_PROJECT_ENTRIES_MAX];
    size_t entry_count;
    bool entries_truncated;
    char preview[16385];
    char preview_path[4096];
    bool preview_truncated;
    char message[AIMT_MESSAGE_MAX_BYTES + 1u];
    char notice[512];
    char ollama[256];
    char llama[256];
    aimt_discovery discovery;
    discovery_job job;
    aimt_codex *codex;
    bool use_codex;
    bool codex_was_busy;
    int execution_mode;
    int save_examples;
    char model_search[128], selected_model[256];
    char local_model[256], local_answer[AIMT_LOCAL_OUTPUT_MAX+1];
    char coordinator_model[256], active_task[AIMT_MESSAGE_MAX_BYTES+1];
    aimt_local_job *local_inference;
    char codex_model[128];
    char codex_notice[512];
    struct nk_font *normal;
    struct nk_font *heading;
} desktop;

static const struct nk_color accent = {153, 191, 255, 255};
static const struct nk_color muted = {171, 185, 204, 255};
static const struct nk_color green = {124, 203, 177, 255};

static void set_notice(desktop *ui, const char *text)
{
    snprintf(ui->notice, sizeof(ui->notice), "%s", text);
}

static bool model_matches(const char *text,const char *query)
{
    if(!query[0])return true;
    for(size_t i=0;text[i];i++) {
        size_t j=0;
        while(query[j] && text[i+j] && tolower((unsigned char)query[j])==tolower((unsigned char)text[i+j]))j++;
        if(!query[j])return true;
    }
    return false;
}

static void open_model_search(desktop *ui,bool gguf)
{
    CURL *curl=curl_easy_init();if(!curl)return;
    char *query=curl_easy_escape(curl,ui->model_search,0);
    if(query) {
        char url[512];
        snprintf(url,sizeof(url),"%s%s",gguf?"https://huggingface.co/models?library=gguf&search=":"https://ollama.com/search?q=",query);
        if(SDL_OpenURL(url))set_notice(ui,"Could not open the search page in a browser.");
        curl_free(query);
    }
    curl_easy_cleanup(curl);
}

static int discover_thread(void *data)
{
    discovery_job *job = data;
    aimt_discover(job->ollama, job->llama, &job->result);
    SDL_AtomicSet(&job->finished, 1);
    return 0;
}

static void start_discovery(desktop *ui)
{
    if (ui->job.thread != NULL) return;
    if (!aimt_local_endpoint_valid(ui->ollama) || !aimt_local_endpoint_valid(ui->llama)) {
        set_notice(ui, "Addresses must use http://127.0.0.1:PORT or http://[::1]:PORT.");
        return;
    }
    snprintf(ui->job.ollama, sizeof(ui->job.ollama), "%s", ui->ollama);
    snprintf(ui->job.llama, sizeof(ui->job.llama), "%s", ui->llama);
    ui->job.creating_profile = false; ui->job.pulling_model = false;
    SDL_AtomicSet(&ui->job.finished, 0);
    ui->job.thread = SDL_CreateThread(discover_thread, "model-discovery", &ui->job);
    if (ui->job.thread == NULL) set_notice(ui, "Could not start model discovery.");
}

static int create_profile_thread(void *data)
{
    discovery_job *job = data;
    job->profile_success = aimt_ollama_profile_create(job->ollama, &job->profile, job->notice, sizeof(job->notice));
    aimt_discover(job->ollama, job->llama, &job->result);
    SDL_AtomicSet(&job->finished, 1);
    return 0;
}
static void start_profile(desktop *ui)
{
    if (ui->job.thread) { set_notice(ui, "Wait for the current Ollama operation."); return; }
    if (!aimt_plugin_validate(&ui->editing)) { set_notice(ui, "Correct the module parameters."); return; }
    aimt_setting *model = aimt_plugin_setting(&ui->editing, "model");
    if (strlen(model->text) >= sizeof(ui->job.profile.base)) { set_notice(ui, "The model identifier is too long."); return; }
    ui->job.profile = (aimt_ollama_profile){
        .context = (unsigned)aimt_plugin_setting(&ui->editing, "context_tokens")->integer,
        .output = (unsigned)aimt_plugin_setting(&ui->editing, "output_tokens")->integer,
        .temperature = aimt_plugin_setting(&ui->editing, "temperature")->decimal};
    memcpy(ui->job.profile.base, model->text, strlen(model->text) + 1);
    snprintf(ui->job.profile.name, sizeof(ui->job.profile.name), "aimt_%s:%s", ui->editing.id,
        ui->policy.profile == AIMT_PROFILE_PERFORMANCE ? "performance" : "balanced");
    char body[1024];
    if (!aimt_ollama_profile_json(&ui->job.profile, body, sizeof(body))) {
        set_notice(ui, "Select a local base model. A profile cannot inherit from itself."); return;
    }
    snprintf(ui->job.module_id, sizeof(ui->job.module_id), "%s", ui->editing.id);
    snprintf(ui->job.ollama, sizeof(ui->job.ollama), "%s", ui->ollama);
    snprintf(ui->job.llama, sizeof(ui->job.llama), "%s", ui->llama);
    ui->job.creating_profile = true; ui->job.pulling_model = false;
    SDL_AtomicSet(&ui->job.finished, 0);
    ui->job.thread = SDL_CreateThread(create_profile_thread, "ollama-profile", &ui->job);
    set_notice(ui, ui->job.thread ? "Saving a separate Ollama profile: context, output and temperature..." : "Could not start saving the profile.");
}

static bool pull_cancelled(void *data)
{
    return SDL_AtomicGet(&((discovery_job *)data)->cancel_pull) != 0;
}
static void pull_progress(void *data, int percent)
{
    SDL_AtomicSet(&((discovery_job *)data)->pull_percent, percent);
}
static int pull_thread(void *data)
{
    discovery_job *job = data;
    job->profile_success = aimt_ollama_pull(job->ollama, job->pull_model, pull_cancelled, pull_progress,
        job, job->notice, sizeof(job->notice));
    aimt_discover(job->ollama, job->llama, &job->result);
    SDL_AtomicSet(&job->finished, 1); return 0;
}
static void start_pull(desktop *ui)
{
    if (ui->job.thread || ui->pending_download >= ui->catalog.count) {
        set_notice(ui, "Wait for the current local service operation."); return;
    }
    const aimt_catalog_model *m = &ui->catalog.models[ui->pending_download];
    snprintf(ui->job.pull_model, sizeof(ui->job.pull_model), "%s", m->id);
    snprintf(ui->job.ollama, sizeof(ui->job.ollama), "%s", ui->ollama);
    snprintf(ui->job.llama, sizeof(ui->job.llama), "%s", ui->llama);
    ui->job.creating_profile = false; ui->job.pulling_model = true;
    SDL_AtomicSet(&ui->job.cancel_pull, 0); SDL_AtomicSet(&ui->job.pull_percent, -1); SDL_AtomicSet(&ui->job.finished, 0);
    ui->job.thread = SDL_CreateThread(pull_thread, "ollama-pull", &ui->job);
    set_notice(ui, ui->job.thread ? "Ollama is downloading the selected model. You can cancel the request in the download panel." : "Could not start the download.");
}

static void poll_discovery(desktop *ui)
{
    if (ui->job.thread != NULL && SDL_AtomicGet(&ui->job.finished)) {
        SDL_WaitThread(ui->job.thread, NULL);
        ui->job.thread = NULL;
        ui->discovery = ui->job.result;
        if (ui->job.pulling_model) set_notice(ui, ui->job.notice);
        else if (ui->job.creating_profile) {
            if (ui->job.profile_success && !strcmp(ui->editing.id, ui->job.module_id)) {
                aimt_setting *model = aimt_plugin_setting(&ui->editing, "model");
                if (!strcmp(model->text, ui->job.profile.base)) snprintf(model->text, sizeof(model->text), "%s", ui->job.profile.name);
            }
            set_notice(ui, ui->job.notice);
        } else set_notice(ui, ui->discovery.status);
    }
}

static void refresh_files(desktop *ui)
{
    if (!aimt_project_list(&ui->project, ui->relative, ui->entries,
                           &ui->entry_count, &ui->entries_truncated)) {
        ui->entry_count = 0;
        set_notice(ui, "Cannot read the project directory.");
    }
}

static bool join_relative(const char *parent, const char *name, char *out, size_t size)
{
    int n = snprintf(out, size, "%s%s%s", parent, parent[0] ? "/" : "", name);
    return n >= 0 && (size_t)n < size;
}

static bool select_project(desktop *ui, const char *path)
{
    if (aimt_codex_get_status(ui->codex)->busy || ui->local_inference) {
        set_notice(ui, "Stop the Codex task before switching projects."); return false;
    }
    if (!aimt_project_open(&ui->project, path)) {
        set_notice(ui, "Cannot open this folder. The previous project remains active.");
        return false;
    }
    snprintf(ui->project_input, sizeof(ui->project_input), "%s", ui->project.root);
    if(!aimt_project_book_add(&ui->projects,ui->project.root))set_notice(ui,"The project list is full or the name is too long.");
    else if(!aimt_project_book_save(ui->projects_path,&ui->projects))set_notice(ui,"The project list is in RAM; its settings file was not saved.");
    /* Each switch starts with read access; the user may explicitly enable edit. */
    size_t selected=aimt_project_book_find(&ui->projects,ui->project.root);
    bool edit=selected!=SIZE_MAX && ui->projects.projects[selected].allow_write;
    if(!edit) {
        if(ui->execution_mode==3 || ui->execution_mode==4)ui->execution_mode=2;
        else if(ui->execution_mode==6)ui->execution_mode=5;
    }
    ui->relative[0] = '\0'; ui->preview[0] = '\0'; ui->preview_path[0] = '\0';
    if (aimt_codex_get_status(ui->codex)->connected) aimt_codex_disconnect(ui->codex);
    aimt_codex_clear(ui->codex); ui->local_answer[0] = '\0'; aimt_app_clear(ui->app);
    refresh_files(ui);
    set_notice(ui, "Working folder selected. Reconnect Codex to use the new access scope.");
    return true;
}

static void browse_project(desktop *ui, const char *path)
{
    if (!aimt_project_open(&ui->picker, path)) {
        set_notice(ui, "Cannot open the folder in the selection dialog."); return;
    }
    snprintf(ui->picker_input, sizeof(ui->picker_input), "%s", ui->picker.root);
    if (!aimt_project_list(&ui->picker, "", ui->picker_entries,
                           &ui->picker_count, &ui->picker_truncated)) ui->picker_count = 0;
}

static bool create_picker_folder(desktop *ui)
{
    char child[4096];
    if(ui->creating_project && (aimt_codex_get_status(ui->codex)->busy || ui->local_inference)) {
        set_notice(ui,"Stop the task before creating a project.");return false;
    }
    if(ui->creating_project && (ui->projects.count>=AIMT_PROJECT_BOOK_MAX || strlen(ui->new_folder)>=sizeof(ui->projects.projects[0].name))) {
        set_notice(ui,"The project list is full or the project name exceeds 95 bytes.");return false;
    }
    if (!join_relative(ui->picker.root, ui->new_folder, child, sizeof(child))) {
        set_notice(ui, "The new folder path is too long."); return false;
    }
    if (!aimt_project_create_folder(&ui->picker, ui->new_folder)) {
        set_notice(ui, errno == EEXIST ? "This name already exists. Enter a different folder name." :
            errno == EINVAL ? "Enter a folder name without /, a leading dot or control characters." :
            "Cannot create the folder. Check permissions and available disk space.");
        return false;
    }
    ui->new_folder[0] = '\0';
    browse_project(ui, child);
    if(ui->creating_project) {
        if(!select_project(ui,child))return false;
        set_notice(ui,"Created and selected the new project. You can start a conversation and enable AI editing.");
        return true;
    }
    set_notice(ui, "Folder created. Click Select this folder to use it as a project.");
    return false;
}

static void open_terminal(desktop *ui)
{
    if (ui->project.root_fd < 0) { set_notice(ui, "Select a working folder first."); return; }
    if (ui->terminal_pid > 0) {
        set_notice(ui, "A terminal opened by this application is already running. Close it before opening another."); return;
    }
    /* Fixed executables and separate argv elements: no shell command interpolation. */
    const char *paths[] = {"/usr/bin/konsole", "/usr/bin/gnome-terminal",
                          "/usr/bin/xfce4-terminal", "/usr/bin/xterm"};
    char *konsole[] = {"konsole", "--separate", "--workdir", ui->project.root, NULL};
    char *gnome[] = {"gnome-terminal", "--wait", "--working-directory", ui->project.root, NULL};
    char *xfce[] = {"xfce4-terminal", "--disable-server", "--working-directory", ui->project.root, NULL};
    char *xterm[] = {"xterm", "-title", "AI Multitool Agents Manager - Terminal", "-e", "/bin/sh", "-c",
                      "cd -- \"$1\" && exec /bin/sh", "aimt-terminal", ui->project.root, NULL};
    char **args[] = {konsole, gnome, xfce, xterm};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (access(paths[i], X_OK) != 0) continue;
        posix_spawn_file_actions_t actions;
        int error = posix_spawn_file_actions_init(&actions);
        if (error) { set_notice(ui, "Could not prepare the terminal."); return; }
        error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        if (!error) error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        if (!error) error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        if (!error) error = posix_spawn(&ui->terminal_pid, paths[i], &actions, NULL, args[i], environ);
        posix_spawn_file_actions_destroy(&actions);
        if (error) { ui->terminal_pid = 0; set_notice(ui, "Could not start the terminal."); }
        else set_notice(ui, "Opened a user terminal in the project folder.");
        return;
    }
    set_notice(ui, "No terminal found. Install Konsole, GNOME Terminal, xfce4-terminal or xterm.");
}

static void choose_plugin(desktop *ui, int selected)
{
    if (selected < 0 || (size_t)selected >= ui->registry.count) return;
    if (ui->editing.id[0]) {
        if (!aimt_plugin_validate(&ui->editing)) {
            set_notice(ui, "Correct the current module values before switching.");
            return;
        }
        ui->registry.plugins[ui->plugin] = ui->editing;
    }
    ui->plugin = selected;
    ui->editing = ui->registry.plugins[selected];
    ui->task = aimt_task_for_plugin(ui->editing.id);
    ui->reset_settings_scroll = true;
}

static void theme(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];
    for (size_t i = 0; i < NK_COLOR_COUNT; ++i) table[i] = nk_rgb(46, 56, 69);
    table[NK_COLOR_TEXT] = nk_rgb(221, 228, 237);
    table[NK_COLOR_WINDOW] = nk_rgb(21, 28, 37);
    table[NK_COLOR_HEADER] = nk_rgb(21, 28, 37);
    table[NK_COLOR_BORDER] = nk_rgb(42, 52, 66);
    table[NK_COLOR_BUTTON] = nk_rgb(40, 51, 65);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgb(57, 70, 87);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(75, 88, 106);
    table[NK_COLOR_TOGGLE] = nk_rgb(44, 56, 70);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgb(64, 77, 94);
    table[NK_COLOR_TOGGLE_CURSOR] = accent;
    table[NK_COLOR_SELECT] = nk_rgb(40, 51, 65);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgb(90, 69, 46);
    table[NK_COLOR_SLIDER] = nk_rgb(45, 55, 67);
    table[NK_COLOR_SLIDER_CURSOR] = accent;
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(255, 194, 130);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = accent;
    table[NK_COLOR_PROPERTY] = nk_rgb(29, 38, 50);
    table[NK_COLOR_EDIT] = nk_rgb(14, 20, 28);
    table[NK_COLOR_EDIT_CURSOR] = accent;
    table[NK_COLOR_COMBO] = nk_rgb(29, 38, 50);
    table[NK_COLOR_CHART] = nk_rgb(29, 38, 50);
    table[NK_COLOR_CHART_COLOR] = green;
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = accent;
    table[NK_COLOR_SCROLLBAR] = nk_rgb(21, 28, 37);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(56, 70, 88);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(72, 89, 111);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = accent;
    table[NK_COLOR_TAB_HEADER] = nk_rgb(29, 38, 50);
    nk_style_from_table(ctx, table);
    ctx->style.window.padding = nk_vec2(10, 8);
    ctx->style.window.spacing = nk_vec2(6, 5);
    ctx->style.window.border = 1;
    ctx->style.window.rounding = 2;
    ctx->style.button.rounding = 2;
    ctx->style.button.padding = nk_vec2(6, 4);
    ctx->style.edit.rounding = 2;
    ctx->style.combo.rounding = 2;
    ctx->style.property.rounding = 2;
    ctx->style.window.group_padding = nk_vec2(8, 6);
}

static void heading(struct nk_context *ctx, desktop *ui, const char *text)
{
    nk_style_set_font(ctx, &ui->heading->handle);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_label(ctx, text, NK_TEXT_LEFT);
    nk_style_set_font(ctx, &ui->normal->handle);
}

static void label_muted(struct nk_context *ctx, const char *text)
{
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, muted);
}

static void draw_header(struct nk_context *ctx, desktop *ui, int width)
{
    if (nk_begin(ctx, "header", nk_rect(6, 6, (float)width - 12, 44), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_begin(ctx, NK_STATIC, 26, 5);
        nk_layout_row_push(ctx, 180);
        nk_style_set_font(ctx, &ui->heading->handle);
        nk_label_colored(ctx, "AGENTS MANAGER", NK_TEXT_LEFT, accent);
        nk_style_set_font(ctx, &ui->normal->handle);
        nk_layout_row_push(ctx, (float)width - 864);
        nk_label_colored(ctx, "Specialized agents. One workspace.", NK_TEXT_LEFT, muted);
        nk_layout_row_push(ctx, 240);
        const char *profiles[] = {"Max. performance", "Balanced", "Security"};
        int profile = nk_combo(ctx, profiles, 3, (int)ui->policy.profile, 30, nk_vec2(260, 160));
        if (profile != (int)ui->policy.profile) {
            if (aimt_policy_set_profile(&ui->policy, (aimt_profile)profile, false) != AIMT_OK)
                set_notice(ui, "The security profile requires a complete Safe Mode. The profile was not changed.");
            else set_notice(ui, profile == AIMT_PROFILE_BALANCED ? "Profile: balanced. Basic access controls remain active." :
                "Profile: maximum performance. The model resource scheduler is not implemented yet.");
        }
        nk_layout_row_push(ctx, 190);
        if (nk_button_label(ctx, "New conversation")) {
            if (!ui->local_inference && aimt_codex_clear(ui->codex)) {
                ui->local_answer[0] = '\0'; aimt_app_clear(ui->app); set_notice(ui, "Started a new conversation.");
            } else set_notice(ui, "Stop the Codex task before starting a new conversation.");
        }
        nk_layout_row_push(ctx, 190);
        if (nk_button_label(ctx, ui->show_settings ? "Hide settings" : "Settings"))
            ui->show_settings = !ui->show_settings;
        nk_layout_row_end(ctx);
    }
    nk_end(ctx);
}

static bool module_ready(const desktop *ui, const aimt_plugin *plugin)
{
    if (ui->use_codex && aimt_codex_get_status(ui->codex)->authenticated &&
        !(plugin->required_capabilities & ~AIMT_MODEL_COMPLETION)) return true;
    if (!aimt_discovery_supports(&ui->discovery, plugin->required_capabilities)) return false;
    const aimt_plugin *current = !strcmp(plugin->id, ui->editing.id) ? &ui->editing : plugin;
    const aimt_setting *chosen = NULL;
    for (size_t i = 0; i < current->setting_count; ++i)
        if (!strcmp(current->settings[i].key, "model")) chosen = &current->settings[i];
    if (!chosen || !chosen->text[0] || (plugin->required_capabilities & ~15u)) return true;
    for (size_t i = 0; i < ui->discovery.count; ++i)
        if (!strcmp(ui->discovery.models[i].id, chosen->text) && ui->discovery.models[i].capabilities_known &&
            (ui->discovery.models[i].capabilities & plugin->required_capabilities) == plugin->required_capabilities) return true;
    return false;
}
static void open_module(desktop *ui, int index)
{
    choose_plugin(ui, index); ui->workspace_tab = 0; ui->show_settings = true;
    if (module_ready(ui, &ui->editing)) ui->settings_tab = 0;
    else {
        ui->settings_tab = 5;
        set_notice(ui, "No matching model. Select a model, download it manually and refresh discovery.");
    }
    ui->reset_settings_scroll = true;
}

static void draw_modules(struct nk_context *ctx, desktop *ui, int width)
{
    if (nk_begin(ctx, "modules", nk_rect(6, 56, (float)width - 12, 42), NK_WINDOW_NO_SCROLLBAR)) {
        size_t visible = ui->registry.count < 5 ? ui->registry.count : 5;
        nk_layout_row_dynamic(ctx, 25, (int)visible + 2 + (ui->registry.count > visible));
        for (size_t i = 0; i < visible; ++i) {
            nk_bool selected = (int)i == ui->plugin;
            bool ready = module_ready(ui, &ui->registry.plugins[i]);
            struct nk_style_selectable saved = ctx->style.selectable;
            if (!ready) {
                ctx->style.selectable.text_normal = nk_rgb(117, 126, 139);
                ctx->style.selectable.text_hover = muted;
                ctx->style.selectable.text_normal_active = muted;
                ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(37, 42, 49));
            }
            bool clicked = nk_selectable_label(ctx, ui->registry.plugins[i].title, NK_TEXT_CENTERED, &selected);
            if (!ready && nk_widget_is_hovered(ctx)) aimt_ui_tooltip(ctx, "No matching model. Click to select a model or configure a local engine.");
            ctx->style.selectable = saved;
            if (clicked) open_module(ui, (int)i);
        }
        if (ui->registry.count > visible && nk_combo_begin_label(ctx, "More modules", nk_vec2(260, 320))) {
            nk_layout_row_dynamic(ctx, 26, 1);
            for (size_t i = visible; i < ui->registry.count; ++i) {
                struct nk_style_button old = ctx->style.contextual_button;
                bool ready = module_ready(ui, &ui->registry.plugins[i]);
                if (!ready) {
                    ctx->style.contextual_button.text_normal = nk_rgb(117, 126, 139);
                    ctx->style.contextual_button.text_hover = muted;
                }
                char label[180]; snprintf(label, sizeof(label), "%s%s", ui->registry.plugins[i].title, ready ? "" : " / no model");
                bool clicked = nk_combo_item_label(ctx, label, NK_TEXT_LEFT);
                ctx->style.contextual_button = old;
                if (clicked) open_module(ui, (int)i);
            }
            nk_combo_end(ctx);
        }
        if (nk_button_label(ctx, "Choose model")) {
            ui->show_settings = true; ui->settings_tab = 4; ui->reset_settings_scroll = true;
        }
        if (nk_button_label(ctx, "Edit module")) {
            ui->show_settings = true; ui->settings_tab = 0; ui->edit_details = true; ui->reset_settings_scroll = true;
        }
    }
    nk_end(ctx);
}

static void draw_sidebar(struct nk_context *ctx, desktop *ui, int height)
{
    if (nk_begin(ctx, "sidebar", nk_rect(6, 104, 226, (float)height - 142), NK_WINDOW_NO_SCROLLBAR)) {
        heading(ctx, ui, "Project");
        const aimt_codex_status *state=aimt_codex_get_status(ui->codex);
        bool busy=state->busy || ui->local_inference;
        nk_layout_row_dynamic(ctx,30,1);
        if(busy)nk_widget_disable_begin(ctx);
        if(nk_combo_begin_label(ctx,"Project list",nk_vec2(400,280))) {
            nk_layout_row_dynamic(ctx,28,1);
            for(size_t i=0;i<ui->projects.count;i++) {
                char label[160];snprintf(label,sizeof(label),"%s · %s",ui->projects.projects[i].name,ui->projects.projects[i].allow_write?"edit":"read-only");
                if(nk_combo_item_label(ctx,label,NK_TEXT_LEFT))select_project(ui,ui->projects.projects[i].path);
            }
            nk_combo_end(ctx);
        }
        size_t active=aimt_project_book_find(&ui->projects,ui->project.root);
        nk_layout_row_dynamic(ctx,24,1);
        if(active!=SIZE_MAX) {
            int edit=ui->projects.projects[active].allow_write;
            if(nk_checkbox_label(ctx,"Allow AI to edit this project",&edit)) {
                ui->projects.projects[active].allow_write=edit!=0;
                aimt_project_book_save(ui->projects_path,&ui->projects);
                aimt_codex_disconnect(ui->codex);aimt_codex_clear(ui->codex);
                if(!edit) {
                    if(ui->execution_mode==3 || ui->execution_mode==4)ui->execution_mode=2;
                    else if(ui->execution_mode==6)ui->execution_mode=5;
                }
            }
        }
        nk_widget_disable_end(ctx);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ui->project_input,
                                      sizeof(ui->project_input), nk_filter_default);
        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, "Open")) {
            select_project(ui, ui->project_input);
        }
        if (nk_button_label(ctx, "Refresh")) refresh_files(ui);
        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, "Select...")) {
            browse_project(ui, ui->project.root_fd >= 0 ? ui->project.root : ".");
            ui->new_folder[0] = '\0';
            ui->creating_project = false;
            ui->show_project_picker = true;
        }
        if (nk_widget_is_hovered(ctx)) aimt_ui_tooltip(ctx, "Select the project working folder.");
        if (nk_button_label(ctx, "Terminal")) open_terminal(ui);
        nk_layout_row_dynamic(ctx,30,1);
        if(busy)nk_widget_disable_begin(ctx);
        if(nk_button_label(ctx,"New project")) {
            char parent[4096];
            const char *base=ui->project.root_fd>=0 && join_relative(ui->project.root,"..",parent,sizeof(parent))?parent:".";
            browse_project(ui,base);
            ui->new_folder[0]=0;ui->creating_project=true;ui->show_project_picker=true;
        }
        nk_widget_disable_end(ctx);
        label_muted(ctx, ui->relative[0] ? ui->relative : "Root directory");
        nk_layout_row_dynamic(ctx, (float)height - 433, 1);
        if (nk_group_begin(ctx, "files", NK_WINDOW_BORDER)) {
            if (ui->relative[0]) {
                nk_layout_row_dynamic(ctx, 28, 1);
                if (nk_button_label(ctx, ".. / parent directory")) {
                    char *slash = strrchr(ui->relative, '/');
                    if (slash) *slash = '\0'; else ui->relative[0] = '\0';
                    refresh_files(ui);
                }
            }
            for (size_t i = 0; i < ui->entry_count; ++i) {
                char label[280];
                aimt_project_entry entry = ui->entries[i];
                snprintf(label, sizeof(label), "%s  %s", entry.symlink ? "[link]" : entry.directory ? "+" : "-", entry.name);
                nk_layout_row_dynamic(ctx, 27, 1);
                if (nk_button_label(ctx, label)) {
                    char target[4096];
                    if (entry.symlink) set_notice(ui, "The preview does not follow links.");
                    else if (!join_relative(ui->relative, entry.name, target, sizeof(target))) set_notice(ui, "The path is too long.");
                    else if (entry.directory) {
                        snprintf(ui->relative, sizeof(ui->relative), "%s", target); refresh_files(ui); break;
                    } else if (aimt_project_read(&ui->project, target, ui->preview,
                               sizeof(ui->preview), &ui->preview_truncated)) {
                        snprintf(ui->preview_path, sizeof(ui->preview_path), "%s", target);
                        ui->workspace_tab = 1;
                        set_notice(ui, ui->preview_truncated ? "Preview limited to 16 KiB." : "File preview opened. Preview editing is not implemented yet.");
                    } else set_notice(ui, "File unavailable, binary or using an unsupported encoding.");
                }
            }
            if (ui->entries_truncated) label_muted(ctx, "Limit: 256 entries.");
            nk_group_end(ctx);
        }
        if (ui->show_project_picker && nk_popup_begin(ctx, NK_POPUP_STATIC,
            ui->creating_project?"New project - select a directory and enter a name":"Select working folder", NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR,
            nk_rect(235, 20, 560, 530))) {
            nk_layout_row_dynamic(ctx, 28, 1);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ui->picker_input,
                sizeof(ui->picker_input), nk_filter_default);
            nk_layout_row_dynamic(ctx, 30, 3);
            if (nk_button_label(ctx, "Go")) browse_project(ui, ui->picker_input);
            if (nk_button_label(ctx, "Parent directory")) {
                char parent[4096];
                if (join_relative(ui->picker.root, "..", parent, sizeof(parent))) browse_project(ui, parent);
            }
            if (nk_button_label(ctx, "Home directory")) {
                const char *home = getenv("HOME");
                if (home) browse_project(ui, home);
            }
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_label(ctx, ui->picker.root, NK_TEXT_LEFT);
            /* Nuklear tooltips are popups too; nesting one here aborts.
             * The full path is available in the editable field above. */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 30, 2);
            nk_layout_row_push(ctx, 0.7f);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ui->new_folder,
                sizeof(ui->new_folder), nk_filter_default);
            nk_layout_row_push(ctx, 0.3f);
            if (nk_button_label(ctx, ui->creating_project?"Create project":"New folder") && create_picker_folder(ui)) {
                ui->show_project_picker=false;nk_popup_close(ctx);
            }
            nk_layout_row_end(ctx);
            nk_layout_row_dynamic(ctx, 296, 1);
            if (nk_group_begin(ctx, "picker-folders", NK_WINDOW_BORDER)) {
                for (size_t i = 0; i < ui->picker_count; ++i) {
                    const aimt_project_entry *entry = &ui->picker_entries[i];
                    if (!entry->directory || entry->symlink) continue;
                    nk_layout_row_dynamic(ctx, 28, 1);
                    if (nk_button_label(ctx, entry->name)) {
                        char child[4096];
                        if (join_relative(ui->picker.root, entry->name, child, sizeof(child))) browse_project(ui, child);
                        break;
                    }
                }
                if (ui->picker_truncated) label_muted(ctx, "Limit: 256 entries. You can enter a path.");
                nk_group_end(ctx);
            }
            nk_layout_row_dynamic(ctx, 22, 1);
            nk_label(ctx, ui->creating_project?"Enter a name above to create a new, empty project.":"Switching folders starts a new Codex conversation.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 30, 2);
            if(ui->creating_project)nk_widget_disable_begin(ctx);
            if (nk_button_label(ctx, "Select this folder") && !ui->creating_project && select_project(ui, ui->picker.root)) {
                ui->show_project_picker = false; nk_popup_close(ctx);
            }
            nk_widget_disable_end(ctx);
            if (nk_button_label(ctx, "Cancel")) {
                ui->show_project_picker = false; nk_popup_close(ctx);
            }
            nk_popup_end(ctx);
        }
    }
    nk_end(ctx);
}

static const char *choose_local_model(desktop *ui,const char *task,bool coordinator)
{
    if(coordinator && ui->coordinator_model[0])return ui->coordinator_model;
    if(!coordinator && ui->local_model[0])return ui->local_model;
    aimt_setting *ram=aimt_plugin_setting(&ui->editing,"ram_mib");
    aimt_setting *vram=aimt_plugin_setting(&ui->editing,"vram_mib");
    aimt_task role=coordinator?AIMT_TASK_PLAN:aimt_task_from_text(task,aimt_task_for_plugin(ui->editing.id));
    size_t index=aimt_model_select(&ui->catalog,&ui->discovery,role,
        ram?(unsigned)ram->integer:8192,vram?(unsigned)vram->integer:6144,coordinator);
    return index==SIZE_MAX?"":ui->discovery.models[index].id;
}

static bool local_coordinator(desktop *ui,const char *task,bool edit)
{
    if(ui->local_inference)return false;
    size_t active=aimt_project_book_find(&ui->projects,ui->project.root);
    if(edit && (active==SIZE_MAX || !ui->projects.projects[active].allow_write)) {
        set_notice(ui,"This project is read-only. Enable editing for the active project.");return false;
    }
    const char *model=choose_local_model(ui,task,false);
    const char *coordinator=choose_local_model(ui,task,true);
    snprintf(ui->selected_model,sizeof(ui->selected_model),"%s",model);
    aimt_setting *context=aimt_plugin_setting(&ui->editing,"context_tokens");
    unsigned tokens=context?(unsigned)context->integer:8192;
    if(tokens<2048 || tokens>32768) {
        set_notice(ui,"The local coordinator supports 2048-32768 context tokens. Change Context limit in the module settings.");return false;
    }
    aimt_local_options options={.chat=true,.coordinator_context=tokens,.worker_context=tokens};
    ui->local_inference=aimt_local_coordinate_options(ui->ollama,coordinator,model,ui->project.root,task,edit,&options);
    if(!ui->local_inference){set_notice(ui,"No local model fits the RAM budget. Check the service, models and budget, or select a model manually.");return false;}
    aimt_codex_disconnect(ui->codex);ui->use_codex=false;ui->execution_mode=edit?6:5;
    char notice[512];snprintf(notice,sizeof(notice),"Coordinator: %.160s. Worker: %.160s. Context: %u tokens for both roles; Stop cancels the task.",coordinator,model,tokens);
    set_notice(ui,notice);return true;
}

static void send_message(desktop *ui)
{
    size_t active=aimt_project_book_find(&ui->projects,ui->project.root);
    bool edit=ui->execution_mode==3 || ui->execution_mode==4 || ui->execution_mode==6;
    if(edit && (active==SIZE_MAX || !ui->projects.projects[active].allow_write)) {
        set_notice(ui,"The active project is read-only.");return;
    }
    if(ui->execution_mode>=5) {
        if(local_coordinator(ui,ui->message,ui->execution_mode==6))ui->message[0]=0;
        return;
    }
    if (ui->use_codex) {
        if (!ui->policy.cloud_allowed || ui->policy.profile == AIMT_PROFILE_SECURITY) {
            set_notice(ui, "Permission to send messages to Codex has not been granted."); return;
        }
        if (ui->project.root_fd < 0) { set_notice(ui, "Select a project folder first."); return; }
        const aimt_codex_status *state=aimt_codex_get_status(ui->codex);
        if(!state->connected || !state->authenticated) {
            if(local_coordinator(ui,ui->message,ui->execution_mode==3 || ui->execution_mode==4))ui->message[0]=0;
            return;
        }
        if (!aimt_codex_hybrid(ui->codex, ui->execution_mode == 1 || ui->execution_mode == 4, ui->ollama, choose_local_model(ui,ui->message,false), ui->save_examples != 0)) {
            set_notice(ui, "Select an Ollama model in the Codex settings; switching modes is blocked while a task is running."); return;
        }
        if (aimt_codex_send(ui->codex, ui->project.root, ui->codex_model, ui->message, ui->execution_mode == 3 || ui->execution_mode==4)) {
            snprintf(ui->active_task,sizeof(ui->active_task),"%s",ui->message);
            ui->message[0] = '\0'; set_notice(ui, "Task sent to Codex.");
        } else set_notice(ui, aimt_codex_get_status(ui->codex)->busy ? "Codex is already working. Wait or stop the task." :
                          "Not sent. Check the connection, login and message length.");
        return;
    }
    if (ui->local_inference) { set_notice(ui, "The local model is working. Wait or stop it."); return; }
    const char *model=choose_local_model(ui,ui->message,false);
    if(!model[0]) {set_notice(ui,"No local text model fits the RAM budget. Check the service and budget, or select a model manually.");return;}
    if (model[0]) {
        snprintf(ui->selected_model,sizeof(ui->selected_model),"%s",model);
        if(ui->editing.required_capabilities & ~AIMT_MODEL_COMPLETION) {set_notice(ui,"This worker handles text tasks. The selected module requires a different adapter.");return;}
        ui->local_inference = aimt_local_start(ui->ollama, model, ui->message);
        if (!ui->local_inference) { set_notice(ui, "Could not start the local model. Check the task and configuration."); return; }
    }
    aimt_result result = aimt_app_submit(ui->app, ui->message, strlen(ui->message));
    if (result == AIMT_OK) {
        ui->message[0] = '\0';
        char notice[512];snprintf(notice,sizeof(notice),"Task sent to local Ollama: %.256s%s",model,ui->local_model[0]?"":" (automatic selection).");
        set_notice(ui,notice);
    } else { aimt_local_destroy(ui->local_inference); ui->local_inference = NULL;
    if (result == AIMT_FULL) set_notice(ui, "The session is full. History was retained; start a new conversation.");
    else set_notice(ui, "Not sent: empty text or disallowed characters.");
    }
}

static void connect_codex(desktop *ui)
{
    if(ui->execution_mode==0 || ui->execution_mode==5)ui->execution_mode=2;
    else if(ui->execution_mode==6)ui->execution_mode=3;
    ui->use_codex=true;ui->policy.cloud_allowed=true;
    const char *executable = getenv("AIMT_CODEX_BIN");
    if (!executable || !executable[0]) executable = ui->discovery.codex_binary[0] ? ui->discovery.codex_binary : "codex";
    const aimt_codex_status *state = aimt_codex_get_status(ui->codex);
    if (!state->connected && !aimt_codex_connect(ui->codex, executable, ui->project.root, ui->execution_mode == 3 || ui->execution_mode==4)) {
        set_notice(ui, state->status); return;
    }
    if (ui->execution_mode == 0) ui->execution_mode = 2;
    ui->use_codex = true; ui->policy.cloud_allowed = true;
    set_notice(ui, "Codex selected. Submitted tasks and accessed files will be processed by its provider.");
}

static void draw_codex_history(struct nk_context *ctx, desktop *ui, float width)
{
    const char *line = aimt_codex_history(ui->codex);
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length == 0) { nk_layout_row_dynamic(ctx, 12, 1); nk_spacing(ctx, 1); }
        while (length) {
            size_t n = length < 2048 ? length : 2048;
            while (n && !aimt_text_valid(line, n)) n--;
            if (!n) break;
            float pixels = ui->normal->handle.width(ui->normal->handle.userdata, ui->normal->handle.height, line, (int)n);
            int rows = (int)(pixels / (width > 0 ? width : 1)) + 2;
            nk_layout_row_dynamic(ctx, (float)rows * 22, 1); nk_text_wrap(ctx, line, (int)n);
            line += n; length -= n;
        }
        if (!end) break;
        line = end + 1;
    }
}

static void draw_workspace(struct nk_context *ctx, desktop *ui, int width, int height)
{
    float panel_width = (float)width - 250 - (ui->show_settings ? 366 : 0);
    if (nk_begin(ctx, "workspace", nk_rect(238, 104, panel_width, (float)height - 142), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, "Conversation")) ui->workspace_tab = 0;
        if (nk_button_label(ctx, "File preview")) ui->workspace_tab = 1;
        if (ui->workspace_tab == 1) {
            heading(ctx, ui, "Project files");
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_label(ctx, ui->preview_path[0] ? ui->preview_path : "Select a file on the left.", NK_TEXT_LEFT);
            label_muted(ctx, "Read-only preview");
            nk_layout_row_dynamic(ctx, (float)height - 280, 1);
            if (nk_group_begin(ctx, "preview", NK_WINDOW_BORDER)) {
                nk_style_set_font(ctx, &ui->code_font->handle);
                const char *line = ui->preview;
                while (*line) {
                    const char *end = strchr(line, '\n');
                    int n = end ? (int)(end - line) : (int)strlen(line);
                    nk_layout_row_dynamic(ctx, 22, 1);
                    nk_text(ctx, line, n, NK_TEXT_LEFT);
                    if (!end) break;
                    line = end + 1;
                }
                nk_style_set_font(ctx, &ui->normal->handle);
                nk_group_end(ctx);
            }
        } else {
            heading(ctx, ui, ui->editing.title);
            const aimt_codex_status *codex = aimt_codex_get_status(ui->codex);
            nk_layout_row_dynamic(ctx, 30, 3);
            if (nk_button_label(ctx, ui->use_codex ? "Codex settings" : "Connect Codex")) {
                if (!ui->use_codex) connect_codex(ui);
                ui->show_settings = true; ui->settings_tab = 7; ui->reset_settings_scroll = true;
            }
            if (codex->busy || ui->local_inference) nk_widget_disable_begin(ctx);
            const char *access[] = {"Local model", "Codex coordinates / local worker", "Codex executes / read-only", "Codex executes / edit", "Codex coordinates / agents / edit", "Local coordinator / read-only", "Local coordinator / edit"};
            int mode = nk_combo(ctx, access, 7, ui->execution_mode, 30, nk_vec2(350, 270));
            if (mode != ui->execution_mode) {
                size_t active=aimt_project_book_find(&ui->projects,ui->project.root);
                if(active!=SIZE_MAX && (mode==3 || mode==4 || mode==6)) {
                    ui->projects.projects[active].allow_write=true;
                    aimt_project_book_save(ui->projects_path,&ui->projects);
                }
                ui->execution_mode = mode; ui->use_codex = mode > 0 && mode<5; aimt_codex_disconnect(ui->codex); aimt_codex_clear(ui->codex);
                if (ui->use_codex) connect_codex(ui);
                set_notice(ui, mode == 0 ? "Local model selected. Sending a task will not start Codex." : mode == 1 ? "Codex coordinates. Select an Ollama model in the settings." : "Codex only runs a task after the user sends it.");
            }
            nk_widget_disable_end(ctx);
            if (!codex->busy && !ui->local_inference) nk_widget_disable_begin(ctx);
            if (nk_button_label(ctx, "Stop")) { ui->active_task[0]=0;aimt_codex_interrupt(ui->codex); aimt_local_cancel(ui->local_inference); }
            nk_widget_disable_end(ctx);
            label_muted(ctx, ui->use_codex ? codex->status : !module_ready(ui, &ui->editing) ? "No matching model - open settings" : ui->job.thread ? "Local Ollama operation in progress..." :
                        ui->local_inference ? (ui->execution_mode>=5 ? "Local coordinator is working..." : "Local model is working...") : ui->discovery.count ? "Models discovered - local session" : "Local session - select your model");
            nk_layout_row_dynamic(ctx, (float)height - 398, 1);
            if (nk_group_begin(ctx, "chat", NK_WINDOW_BORDER)) {
                aimt_status status;
                aimt_app_status(ui->app, &status);
                if (ui->use_codex) draw_codex_history(ctx, ui, panel_width - 90);
                if (!ui->use_codex && status.message_count == 0) {
                    nk_layout_row_dynamic(ctx, 28, 1);
                    nk_label_colored(ctx, "Start with your goal.", NK_TEXT_LEFT, accent);
                    nk_layout_row_dynamic(ctx, 64, 1);
                    nk_label_wrap(ctx, "Describe a project, task or problem. Configure the model and module on the right. Project files are on the left.");
                    nk_layout_row_dynamic(ctx, 62, 1);
                    nk_label_colored_wrap(ctx, "Select a worker above the chat and send a task. The conversation stays in RAM; saving examples requires enabling it in Codex settings.", muted);
                }
                for (size_t i = 0; !ui->use_codex && i < status.message_count; ++i) {
                    const char *message = aimt_app_message(ui->app, i);
                    nk_layout_row_dynamic(ctx, 24, 1);
                    nk_label_colored(ctx, "YOU", NK_TEXT_LEFT, accent);
                    float text_width = panel_width - 90;
                    float pixels = ui->normal->handle.width(ui->normal->handle.userdata,
                        ui->normal->handle.height, message, (int)strlen(message));
                    int rows = (int)(pixels / (text_width > 0 ? text_width : 1)) + 2;
                    if (rows > 50) rows = 50;
                    nk_layout_row_dynamic(ctx, (float)rows * 22, 1);
                    nk_label_wrap(ctx, message);
                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_spacing(ctx, 1);
                }
                if (!ui->use_codex && ui->local_answer[0]) {
                    const char *line=ui->local_answer;
                    label_muted(ctx,"LOCAL MODEL / LATEST RESULT");
                    while (*line) {
                        const char *end=strchr(line,'\n');size_t n=end?(size_t)(end-line):strlen(line);
                        while(n) { size_t part=n>1000?1000:n; while(part<n && ((unsigned char)line[part]&0xc0)==0x80)part--;
                            nk_layout_row_dynamic(ctx,70,1);nk_text_wrap(ctx,line,(int)part);line+=part;n-=part; }
                        if(!end)break;
                        line=end+1;
                    }
                }
                nk_group_end(ctx);
            }
            nk_layout_row_dynamic(ctx, 82, 1);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_BOX, ui->message, sizeof(ui->message), nk_filter_default);
            nk_layout_row_dynamic(ctx, 34, 2);
            nk_label_colored(ctx, ui->use_codex ? "Codex - tasks up to 2048 bytes" : "Up to 2048 bytes - no disk persistence", NK_TEXT_LEFT, muted);
            if (nk_button_label(ctx, ui->use_codex ? "Send to Codex" : "Send locally")) send_message(ui);
        }
    }
    nk_end(ctx);
}

static void draw_setting(struct nk_context *ctx, desktop *ui, aimt_setting *s)
{
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_label(ctx, s->label, NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30, 1);
    if (strcmp(s->key, "model") == 0) {
        if (nk_combo_begin_label(ctx, s->text[0] ? s->text : "Select a discovered model", nk_vec2(310, 240))) {
            nk_layout_row_dynamic(ctx, 28, 1);
            if (nk_combo_item_label(ctx, "No model", NK_TEXT_LEFT)) s->text[0] = '\0';
            for (size_t i = 0; i < ui->discovery.count; ++i) {
                char label[320];
                snprintf(label, sizeof(label), "%s / %s", ui->discovery.models[i].provider, ui->discovery.models[i].id);
                if (nk_combo_item_label(ctx, label, NK_TEXT_LEFT)) {
                    snprintf(s->text, sizeof(s->text), "%s", ui->discovery.models[i].id);
                    aimt_setting *provider = aimt_plugin_setting(&ui->editing, "provider");
                    provider->selected = strcmp(ui->discovery.models[i].provider, "Ollama") == 0 ? 1 : 2;
                }
            }
            nk_combo_end(ctx);
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_layout_row_dynamic(ctx, !strcmp(s->key, "instructions") ? 90 : 30, 1);
        nk_edit_string_zero_terminated(ctx, !strcmp(s->key, "instructions") ? NK_EDIT_BOX : NK_EDIT_FIELD, s->text, sizeof(s->text), nk_filter_default);
    } else if (s->type == AIMT_SETTING_TEXT_TYPE) {
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->text, sizeof(s->text), nk_filter_default);
    } else if (s->type == AIMT_SETTING_INT) {
        nk_property_int(ctx, "#", (int)s->minimum, &s->integer, (int)s->maximum,
                        s->maximum <= 1000 ? 1 : 64, 1);
    } else if (s->type == AIMT_SETTING_FLOAT) {
        nk_property_float(ctx, "#", (float)s->minimum, &s->decimal, (float)s->maximum, 0.05f, 0.01f);
    } else if (s->type == AIMT_SETTING_BOOL) {
        nk_checkbox_label(ctx, "Enable", &s->integer);
    } else if (s->type == AIMT_SETTING_CHOICE) {
        const char *choices[AIMT_CHOICE_MAX];
        for (int i = 0; i < s->choice_count; ++i) choices[i] = s->choices[i];
        s->selected = nk_combo(ctx, choices, s->choice_count, s->selected, 28, nk_vec2(290, 240));
    }
    if (s->help[0] && nk_widget_is_hovered(ctx)) aimt_ui_tooltip(ctx, s->help);
}

static void draw_discovery(struct nk_context *ctx, desktop *ui)
{
    nk_layout_row_dynamic(ctx, 34, 1);
    if (nk_button_label(ctx, "Coordinator and cloud assistance")) {
        ui->settings_tab = 3; ui->reset_settings_scroll = true;
    }
    label_muted(ctx, "LOCAL ENGINES");
    nk_layout_row_dynamic(ctx, 22, 1); nk_label(ctx, "Ollama - service address", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ui->ollama, sizeof(ui->ollama), nk_filter_default);
    nk_layout_row_dynamic(ctx, 22, 1); nk_label(ctx, "llama.cpp - service address", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ui->llama, sizeof(ui->llama), nk_filter_default);
    nk_layout_row_dynamic(ctx, 34, 1);
    if (nk_button_label(ctx, ui->job.thread ? "Discovering..." : "Discover again")) start_discovery(ui);
    nk_layout_row_dynamic(ctx, 46, 1);
    nk_label_colored_wrap(ctx, "Discovery reads metadata. It does not download or load models.", muted);
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label_colored(ctx, ui->discovery.ollama_online ? "Ollama / service available" : "Ollama / service unavailable",
                     NK_TEXT_LEFT, ui->discovery.ollama_online ? green : muted);
    nk_layout_row_dynamic(ctx, 48, 1);
    nk_label_colored_wrap(ctx, ui->discovery.ollama_binary[0] ? ui->discovery.ollama_binary : "ollama was not found in PATH.", muted);
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label_colored(ctx, ui->discovery.llama_online ? "llama.cpp / service available" : "llama.cpp / service unavailable",
                     NK_TEXT_LEFT, ui->discovery.llama_online ? green : muted);
    nk_layout_row_dynamic(ctx, 48, 1);
    nk_label_colored_wrap(ctx, ui->discovery.llama_binary[0] ? ui->discovery.llama_binary : "llama-server was not found in PATH.", muted);
    label_muted(ctx, "SEARCH OLLAMA / LLAMA.CPP MODELS");
    nk_layout_row_dynamic(ctx,30,1);
    nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,ui->model_search,sizeof(ui->model_search),nk_filter_default);
    nk_layout_row_dynamic(ctx,30,2);
    if(nk_button_label(ctx,"Search online: Ollama"))open_model_search(ui,false);
    if(nk_button_label(ctx,"Search GGUF: llama.cpp"))open_model_search(ui,true);
    nk_layout_row_dynamic(ctx,44,1);
    nk_label_colored_wrap(ctx,"Online opens the catalog in a browser. Local capabilities below are declared by the engine.",muted);
    label_muted(ctx, "DISCOVERED MODELS");
    for (size_t i = 0; i < ui->discovery.count; ++i) {
        const aimt_discovered_model *model = &ui->discovery.models[i];
        if(!model_matches(model->id,ui->model_search) && !model_matches(model->provider,ui->model_search))continue;
        nk_layout_row_dynamic(ctx, 44, 1);
        nk_label_wrap(ctx, model->id);
        char metadata[256];
        snprintf(metadata, sizeof(metadata), "%s  %s  %s  %.2f GiB (file)", model->provider,
                 model->parameters, model->quantization, (double)model->disk_bytes / 1073741824.0);
        nk_layout_row_dynamic(ctx, 44, 1);
        nk_label_colored_wrap(ctx, metadata, muted);
        nk_layout_row_dynamic(ctx,48,1);
        if(!model->capabilities_known)nk_label_colored_wrap(ctx,"Capabilities unknown - no engine declaration.",muted);
        else {
            char features[256];
            snprintf(features,sizeof(features),"Declared: text %s | images %s | tools %s | embedding %s",
                model->capabilities&AIMT_MODEL_COMPLETION?"yes":"no",model->capabilities&AIMT_MODEL_VISION?"yes":"no",
                model->capabilities&AIMT_MODEL_TOOLS?"yes":"no",model->capabilities&AIMT_MODEL_EMBEDDING?"yes":"no");
            nk_label_colored_wrap(ctx,features,muted);
        }
    }
    if (ui->discovery.count == 0) {
        nk_layout_row_dynamic(ctx, 62, 1);
        nk_label_wrap(ctx, "No models found in the available services. You can enter another port above.");
    }
}

static void draw_hybrid(struct nk_context *ctx, desktop *ui)
{
    label_muted(ctx, "LOCAL MODELS + CLOUD");
    nk_layout_row_dynamic(ctx, 70, 1);
    nk_label_wrap(ctx, "The local model performs the work. Cloud assistance can plan, review results and help after errors. Findings return to the local task.");
    nk_layout_row_dynamic(ctx, 30, 1);
    const char *providers[] = {"ChatGPT Plus / Pro", "OpenAI API", "Other provider"};
    ui->cloud_provider = nk_combo(ctx, providers, 3, ui->cloud_provider, 28, nk_vec2(310, 170));
    nk_layout_row_dynamic(ctx, 110, 1);
    nk_label_colored_wrap(ctx, ui->cloud_provider == 0 ?
        "Codex uses the local app-server and its login. Access depends on your account and limits. Open Codex settings below." :
        "Planned API adapter. Requires your own access and billing. Keys need a protected store, not a plugin manifest.", muted);
    nk_layout_row_dynamic(ctx, 30, 1);
    int allowed = ui->policy.cloud_allowed;
    nk_checkbox_label(ctx, "Allow after connecting an account", &allowed);
    ui->policy.cloud_allowed = allowed != 0;
    nk_layout_row_dynamic(ctx, 30, 1);
    int coordinator = ui->policy.cloud_coordinator;
    nk_checkbox_label(ctx, "Cloud coordinator", &coordinator);
    ui->policy.cloud_coordinator = coordinator != 0;
    nk_layout_row_dynamic(ctx, 30, 1);
    int reviewer = ui->policy.cloud_reviewer;
    nk_checkbox_label(ctx, "Review and improve results", &reviewer);
    ui->policy.cloud_reviewer = reviewer != 0;
    nk_layout_row_dynamic(ctx, 30, 1);
    int fallback = ui->policy.cloud_fallback;
    nk_checkbox_label(ctx, "Assistance after local failure", &fallback);
    ui->policy.cloud_fallback = fallback != 0;
    label_muted(ctx, "Local attempts before assistance");
    nk_layout_row_dynamic(ctx, 30, 1);
    int attempts = (int)ui->policy.local_attempt_limit;
    nk_property_int(ctx, "#", 1, &attempts, 10, 1, 1);
    ui->policy.local_attempt_limit = (unsigned)attempts;
    label_muted(ctx, "Cloud calls per task");
    nk_layout_row_dynamic(ctx, 30, 1);
    int calls = (int)ui->policy.cloud_call_limit;
    nk_property_int(ctx, "#", 1, &calls, 1000, 1, 1);
    ui->policy.cloud_call_limit = (unsigned)calls;
    nk_layout_row_dynamic(ctx, 80, 1);
    nk_label_colored_wrap(ctx, aimt_codex_get_status(ui->codex)->status, accent);
    nk_layout_row_dynamic(ctx, 60, 1);
    nk_label_colored_wrap(ctx, ui->discovery.codex_binary[0] ? ui->discovery.codex_binary :
                         "Codex CLI was not found in PATH. In-app persistent login is not implemented.", muted);
    nk_layout_row_dynamic(ctx, 34, 1);
    if (nk_button_label(ctx, "Open Codex settings")) { ui->settings_tab = 7; ui->reset_settings_scroll = true; }
}

static void draw_codex_settings(struct nk_context *ctx, desktop *ui)
{
    const aimt_codex_status *state = aimt_codex_get_status(ui->codex);
    label_muted(ctx, "CODEX / PROJECT");
    nk_layout_row_dynamic(ctx, 65, 1); nk_label_wrap(ctx, state->status);
    nk_layout_row_dynamic(ctx, 30, 2);
    if (nk_button_label(ctx, "Connect")) connect_codex(ui);
    if (nk_button_label(ctx, "Disconnect")) {
        aimt_codex_disconnect(ui->codex); ui->execution_mode=0; ui->use_codex = false; ui->policy.cloud_connected = false;
        set_notice(ui, "Disconnected Codex and stopped its processes.");
    }
    nk_layout_row_dynamic(ctx, 30, 1);
    if (!state->connected || state->authenticated || state->login_pending || state->busy) nk_widget_disable_begin(ctx);
    if (nk_button_label(ctx, "Log in with ChatGPT")) aimt_codex_login(ui->codex);
    nk_widget_disable_end(ctx);
    if (state->login_url[0]) {
        nk_layout_row_dynamic(ctx, 48, 1); nk_label_wrap(ctx, state->login_url);
        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, "Open login page")) {
            if (SDL_OpenURL(state->login_url) != 0) set_notice(ui, "Could not open the browser. Copy the login address.");
        }
        if (nk_button_label(ctx, "Copy code")) SDL_SetClipboardText(state->login_code);
        label_muted(ctx, state->login_code);
    }
    nk_layout_row_dynamic(ctx, 30, 1);
    int allowed = ui->policy.cloud_allowed;
    nk_checkbox_label(ctx, "Allow sending tasks to Codex", &allowed);
    if (!allowed && ui->policy.cloud_allowed && state->busy) aimt_codex_disconnect(ui->codex);
    ui->policy.cloud_allowed = allowed != 0;
    label_muted(ctx, "Codex model");
    nk_layout_row_dynamic(ctx, 30, 1);
    if (state->busy) nk_widget_disable_begin(ctx);
    if (nk_combo_begin_label(ctx, ui->codex_model[0] ? ui->codex_model : "Default Codex model", nk_vec2(310, 270))) {
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_combo_item_label(ctx, "Default Codex model", NK_TEXT_LEFT)) {
            ui->codex_model[0] = '\0'; aimt_codex_clear(ui->codex);
        }
        for (size_t i = 0; i < state->model_count; ++i) {
            nk_layout_row_dynamic(ctx, 28, 1);
            if (nk_combo_item_label(ctx, state->models[i].id, NK_TEXT_LEFT)) {
                snprintf(ui->codex_model, sizeof(ui->codex_model), "%s", state->models[i].id);
                aimt_codex_clear(ui->codex);
            }
        }
        nk_combo_end(ctx);
    }
    nk_widget_disable_end(ctx);
    label_muted(ctx, "LOCAL WORKER / OLLAMA");
    nk_layout_row_dynamic(ctx, 30, 1);
    if (state->busy || ui->local_inference) nk_widget_disable_begin(ctx);
    if (nk_combo_begin_label(ctx, ui->local_model[0] ? ui->local_model : "Automatic - based on task and budget", nk_vec2(320,270))) {
        nk_layout_row_dynamic(ctx,28,1);
        if(nk_combo_item_label(ctx,"Automatic",NK_TEXT_LEFT)){ui->local_model[0]=0;aimt_codex_clear(ui->codex);}
        for (size_t i=0;i<ui->discovery.count;i++) {
            const aimt_discovered_model *m=&ui->discovery.models[i];
            if (strcmp(m->provider,"Ollama") || strstr(m->id,"cloud")) continue;
            nk_layout_row_dynamic(ctx,28,1);
            if (nk_combo_item_label(ctx,m->id,NK_TEXT_LEFT)) { snprintf(ui->local_model,sizeof(ui->local_model),"%s",m->id); aimt_codex_clear(ui->codex); }
        }
        nk_combo_end(ctx);
    }
    nk_layout_row_dynamic(ctx,30,1);
    nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,ui->local_model,sizeof(ui->local_model),nk_filter_default);
    nk_layout_row_dynamic(ctx,22,1);nk_label(ctx,"Local coordinator (empty = automatic selection)",NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx,30,1);nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,ui->coordinator_model,sizeof(ui->coordinator_model),nk_filter_default);
    nk_layout_row_dynamic(ctx,30,1);
    nk_checkbox_label(ctx,"Save reviewed examples in the project",&ui->save_examples);
    nk_widget_disable_end(ctx);
    nk_layout_row_dynamic(ctx,80,1);
    nk_label_colored_wrap(ctx,"Coordinator: at most 4 local requests per turn and 512 output tokens per request. Reviews stay in conversation memory; optionally save .aimt-learning/examples.jsonl (8 MiB). These are training examples; model weights are unchanged.",muted);
    label_muted(ctx, "Working folder");
    nk_layout_row_dynamic(ctx, 65, 1); nk_label_wrap(ctx, ui->project.root);
    nk_layout_row_dynamic(ctx, 65, 1);
    nk_label_wrap(ctx, "Read-only access is the default. Enable editing above the chat. Changing the folder, model or access starts a new conversation.");
    nk_layout_row_dynamic(ctx, 90, 1);
    nk_label_colored_wrap(ctx, "Codex runs in an isolated process. It reads the selected project and required system files. Writes require edit mode. Commands have no network access; the user terminal is separate.", muted);
    nk_layout_row_dynamic(ctx, 90, 1);
    nk_label_colored_wrap(ctx, "Codex processes tasks and selected files through the configured provider. Codex CLI handles login and account data. The 128 KiB limit applies only to the conversation preview.", muted);
    if (state->activity[0]) {
        nk_layout_row_dynamic(ctx, 70, 1); nk_label_wrap(ctx, state->activity);
    }
}

static void draw_recommendations(struct nk_context *ctx, desktop *ui)
{
    const char *tasks[] = {"Conversation / assistant", "Code / debugging", "Planning / game story", "Translation", "Image / screenshot", "Memory / embedding"};
    nk_layout_row_dynamic(ctx, 30, 1);
    ui->task = (aimt_task)nk_combo(ctx, tasks, AIMT_TASK_COUNT, (int)ui->task, 28, nk_vec2(310, 240));
    aimt_setting *ram = aimt_plugin_setting(&ui->editing, "ram_mib");
    aimt_setting *vram = aimt_plugin_setting(&ui->editing, "vram_mib");
    aimt_setting *context = aimt_plugin_setting(&ui->editing, "context_tokens");
    label_muted(ctx, "Available budget / MiB");
    nk_layout_row_dynamic(ctx, 28, 2);
    nk_property_int(ctx, "RAM", 256, &ram->integer, 131072, 512, 1);
    nk_property_int(ctx, "VRAM", 0, &vram->integer, 131072, 256, 1);
    nk_layout_row_dynamic(ctx, 28, 1);
    nk_property_int(ctx, "Context", 512, &context->integer, 131072, 512, 1);
    nk_layout_row_dynamic(ctx, 48, 1);
    nk_label_colored_wrap(ctx, "Memory estimates require measurement. Order: task, budget, local availability.", muted);
    char date[80]; snprintf(date, sizeof(date), "Sources checked: %s", ui->catalog.verified);
    label_muted(ctx, date);
    aimt_recommendation recommended[AIMT_CATALOG_MAX];
    size_t count = aimt_catalog_recommend(&ui->catalog, &ui->discovery, ui->task,
        (unsigned)ram->integer, (unsigned)vram->integer, (unsigned)context->integer, recommended);
    nk_layout_row_dynamic(ctx,30,1);
    nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,ui->model_search,sizeof(ui->model_search),nk_filter_default);
    if (!count) label_muted(ctx, "No catalog entries for this task.");
    for (size_t i = 0; i < count; ++i) {
        const aimt_recommendation *r = &recommended[i];
        const aimt_catalog_model *m = &ui->catalog.models[r->index];
        if(!model_matches(m->id,ui->model_search) && !model_matches(m->note,ui->model_search))continue;
        nk_layout_row_dynamic(ctx, 27, 1);
        nk_label_colored(ctx, m->id, NK_TEXT_LEFT, accent);
        char summary[160];
        snprintf(summary, sizeof(summary), "%s • %s", r->installed ? "Local" : "Available to download",
            r->gpu_candidate ? "GPU candidate" : r->ram_candidate ? "CPU / partial GPU" : "over budget");
        label_muted(ctx, summary);
        snprintf(summary, sizeof(summary), "File ~%.1f GiB - with reserve ~%.1f GiB", (double)m->weights_mib / 1024,
            (double)r->estimated_mib / 1024);
        label_muted(ctx, summary);
        float text_pixels = ui->normal->handle.width(ui->normal->handle.userdata,
            ui->normal->handle.height, m->note, (int)strlen(m->note));
        struct nk_rect bounds = nk_window_get_content_region(ctx);
        float note_width = bounds.w > 40 ? bounds.w - 24 : 1;
        float note_lines = ceilf(text_pixels / note_width) + 1;
        nk_layout_row_dynamic(ctx, note_lines * 19, 1);
        nk_label_wrap(ctx, m->note);
        nk_layout_row_dynamic(ctx, 24, 1);
        nk_label_colored(ctx, m->license, NK_TEXT_LEFT, muted);
        if (nk_widget_is_hovered(ctx)) aimt_ui_tooltip(ctx, m->source);
        nk_layout_row_dynamic(ctx, 28, 1);
        bool usable = ui->task != AIMT_TASK_EMBED && (r->gpu_candidate || r->ram_candidate);
        if (!usable) nk_widget_disable_begin(ctx);
        char button[160]; snprintf(button, sizeof(button), "%s: %s", r->installed ? "Select" : "Set suggestion", m->id);
        if (nk_button_label(ctx, button) && usable) {
            snprintf(aimt_plugin_setting(&ui->editing, "model")->text, AIMT_SETTING_TEXT, "%s", m->id);
            aimt_plugin_setting(&ui->editing, "provider")->selected = 1;
            set_notice(ui, r->installed ? "Local model selected. Apply or save the module settings." :
                "Suggestion set. Choose a manual or in-app download; nothing has been downloaded.");
        }
        if (!usable) nk_widget_disable_end(ctx);
        if (!r->installed) {
            nk_layout_row_dynamic(ctx, 28, 2);
            if (nk_button_label(ctx, "Download here")) {
                ui->pending_download = r->index; ui->settings_tab = 6; ui->reset_settings_scroll = true;
            }
            if (nk_button_label(ctx, "Manual command")) {
                char command[160]; snprintf(command, sizeof(command), "ollama pull %s", m->id);
                if (SDL_SetClipboardText(command) == 0) set_notice(ui, "ollama pull command copied. Run it in a terminal, then click Discover again.");
                else set_notice(ui, "Could not copy the command.");
            }
        }
        nk_layout_row_dynamic(ctx, 12, 1); nk_spacing(ctx, 1);
    }
}

static void draw_missing_model(struct nk_context *ctx, desktop *ui)
{
    label_muted(ctx, "MODULE UNAVAILABLE / NO MODEL");
    nk_layout_row_dynamic(ctx, 64, 1);
    nk_label_wrap(ctx, "No local model selected or required capabilities unconfirmed. Select a compatible model and refresh discovery after downloading.");
    if (ui->editing.required_capabilities & (AIMT_MODEL_STT | AIMT_MODEL_TTS)) {
        nk_layout_row_dynamic(ctx, 112, 1);
        nk_label_wrap(ctx, "Voice requires an STT model and XTTS v2. Set their local paths in the settings. Voice adapters are not implemented; the presence of files does not activate them.");
    } else if (ui->editing.required_capabilities & (AIMT_MODEL_IMAGE_GENERATION | AIMT_MODEL_MESH_GENERATION)) {
        nk_layout_row_dynamic(ctx, 112, 1);
        nk_label_wrap(ctx, "This task requires a dedicated generation engine and checkpoint. An image understanding model does not generate images or 3D meshes. The runtime adapter is not implemented yet.");
    } else {
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_button_label(ctx, "Select from model catalog")) {
            ui->settings_tab = 4; ui->reset_settings_scroll = true;
        }
    }
    nk_layout_row_dynamic(ctx, 28, 1);
    if (nk_button_label(ctx, "Select model / edit settings")) {
        ui->settings_tab = 0; ui->reset_settings_scroll = true;
    }
    nk_layout_row_dynamic(ctx, 28, 1);
    if (nk_button_label(ctx, ui->job.thread ? "Discovering..." : "Check availability again")) start_discovery(ui);
    nk_layout_row_dynamic(ctx, 85, 1);
    nk_label_colored_wrap(ctx, "Disabled modules indicate model readiness. The form remains available. Runtime adapters for new modules still require implementation even after selecting a model.", muted);
}

static void draw_download(struct nk_context *ctx, desktop *ui)
{
    if (ui->pending_download >= ui->catalog.count) return;
    const aimt_catalog_model *m = &ui->catalog.models[ui->pending_download];
    if (ui->job.thread && ui->job.pulling_model)
        for (size_t i = 0; i < ui->catalog.count; ++i)
            if (!strcmp(ui->catalog.models[i].id, ui->job.pull_model)) m = &ui->catalog.models[i];
    label_muted(ctx, "DOWNLOAD / OLLAMA");
    nk_layout_row_dynamic(ctx, 32, 1); nk_label_colored(ctx, m->id, NK_TEXT_LEFT, accent);
    char text[192]; snprintf(text, sizeof(text), "Approximate size: %.1f GiB. The local Ollama server saves weights in its model directory.", (double)m->weights_mib / 1024);
    nk_layout_row_dynamic(ctx, 74, 1); nk_label_wrap(ctx, text);
    if (ui->job.thread && ui->job.pulling_model) {
        int percent = SDL_AtomicGet(&ui->job.pull_percent);
        snprintf(text, sizeof(text), percent < 0 ? "Preparing / waiting for server" : "Current chunk: %d%%", percent);
        label_muted(ctx, text);
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_button_label(ctx, "Cancel request")) SDL_AtomicSet(&ui->job.cancel_pull, 1);
    } else {
        nk_layout_row_dynamic(ctx, 48, 1);
        nk_label_colored_wrap(ctx, "Downloading uses the Ollama server internet connection. It does not run model inference.", muted);
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Confirm download")) start_pull(ui);
    }
    nk_layout_row_dynamic(ctx, 30, 1);
    if (nk_button_label(ctx, "Back to model selection")) { ui->settings_tab = 4; ui->reset_settings_scroll = true; }
    nk_layout_row_dynamic(ctx, 56, 1);
    nk_label_colored_wrap(ctx, "No automatic downloads at startup. Progress refers to a chunk, not the entire model package.", muted);
}

static void draw_settings(struct nk_context *ctx, desktop *ui, int width, int height)
{
    if (!ui->show_settings) return;
    if (nk_begin(ctx, "settings", nk_rect((float)width - 366, 104, 360, (float)height - 142), NK_WINDOW_NO_SCROLLBAR)) {
        heading(ctx, ui, "Module properties");
        nk_layout_row_dynamic(ctx, 30, 4);
        if (nk_button_label(ctx, "Module")) { ui->settings_tab = 0; ui->reset_settings_scroll = true; }
        if (nk_button_label(ctx, "Suggested")) { ui->settings_tab = 4; ui->reset_settings_scroll = true; }
        if (nk_button_label(ctx, "Engines")) { ui->settings_tab = 1; ui->reset_settings_scroll = true; }
        if (nk_button_label(ctx, "Protection")) { ui->settings_tab = 2; ui->reset_settings_scroll = true; }
        if (ui->reset_settings_scroll) {
            nk_group_set_scroll(ctx, "settings-scroll", 0, 0);
            ui->reset_settings_scroll = false;
        }
        nk_layout_row_dynamic(ctx, (float)height - 292, 1);
        if (nk_group_begin(ctx, "settings-scroll", 0)) {
            if (ui->settings_tab == 0) {
                nk_layout_row_dynamic(ctx, 26, 1);
                nk_label_colored(ctx, ui->editing.implemented ? "Text chat / Ollama and Codex" : "Module runtime: not implemented", NK_TEXT_LEFT, accent);
                nk_layout_row_dynamic(ctx, 28, 1);
                if (nk_button_label(ctx, ui->edit_details ? "Collapse module description" : "Edit name and description")) ui->edit_details = !ui->edit_details;
                if (ui->edit_details) {
                    label_muted(ctx, "Module name");
                    nk_layout_row_dynamic(ctx, 28, 1);
                    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ui->editing.title, sizeof(ui->editing.title), nk_filter_default);
                    label_muted(ctx, "Description");
                    nk_layout_row_dynamic(ctx, 70, 1);
                    nk_edit_string_zero_terminated(ctx, NK_EDIT_BOX, ui->editing.description, sizeof(ui->editing.description), nk_filter_default);
                }
                for (size_t i = 0; i < ui->editing.setting_count; ++i) draw_setting(ctx, ui, &ui->editing.settings[i]);
            } else if (ui->settings_tab == 1) draw_discovery(ctx, ui);
            else if (ui->settings_tab == 4) draw_recommendations(ctx, ui);
            else if (ui->settings_tab == 5) draw_missing_model(ctx, ui);
            else if (ui->settings_tab == 6) draw_download(ctx, ui);
            else if (ui->settings_tab == 3) draw_hybrid(ctx, ui);
            else if (ui->settings_tab == 7) draw_codex_settings(ctx, ui);
            else {
                label_muted(ctx, "RULES FOR THIS VERSION");
                const char *lines[] = {
                    "The manager keeps the conversation preview in RAM. Codex manages its own storage and diagnostics. Module settings are saved separately without encryption.",
                    "A local model is selected by default. Codex starts a task only after selecting its mode and sending a request. The coordinator uses local Ollama; project editing requires a separate choice.",
                    "Ollama operations use loopback ports. A profile changes parameters of a separate alias after you click.",
                    "A plugin manifest describes a form. It cannot grant permissions or execute code.",
                    "Safe Mode is not implemented. Attempts to enable it are rejected; this version does not provide an encrypted workspace."
                };
                for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
                    nk_layout_row_dynamic(ctx, 98, 1); nk_label_wrap(ctx, lines[i]);
                }
            }
            nk_group_end(ctx);
        }
        if (ui->settings_tab == 0) {
            nk_layout_row_dynamic(ctx, 30, 3);
            if (nk_button_label(ctx, "Apply")) {
                if (aimt_plugin_validate(&ui->editing)) {
                    ui->registry.plugins[ui->plugin] = ui->editing;
                    set_notice(ui, "Settings applied in RAM. Resource scheduling and specialized module runtimes remain to be implemented.");
                } else set_notice(ui, "Settings were not applied: invalid value.");
            }
            if (nk_button_label(ctx, "Save")) {
                if (aimt_plugin_settings_save(ui->settings_path, &ui->editing)) {
                    ui->registry.plugins[ui->plugin] = ui->editing;
                    ui->saved_settings++;
                    set_notice(ui, "Module settings saved. They will be loaded at the next start.");
                } else set_notice(ui, "Not saved: check the values and private settings directory (no links).");
            }
            if (nk_button_label(ctx, "Ollama profile")) start_profile(ui);
            label_muted(ctx, "The profile sets context in Ollama");
        }
    }
    nk_end(ctx);
}

static void draw_footer(struct nk_context *ctx, desktop *ui, int width, int height)
{
    if (nk_begin(ctx, "footer", nk_rect(6, (float)height - 32, (float)width - 12, 26), NK_WINDOW_NO_SCROLLBAR)) {
        /* Compact footer has no inner vertical padding. */
        nk_layout_row_dynamic(ctx, 18, 1);
        nk_label_colored(ctx, ui->notice, NK_TEXT_LEFT, muted);
        if (nk_widget_is_hovered(ctx)) aimt_ui_tooltip(ctx, ui->notice);
    }
    nk_end(ctx);
}

static bool screenshot(SDL_Renderer *renderer, int width, int height, const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL) return false;
    bool ok = SDL_RenderReadPixels(renderer, NULL, surface->format->format, surface->pixels, surface->pitch) == 0 &&
              SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    return ok;
}

static void load_fonts(struct nk_context *ctx, desktop *ui, SDL_Renderer *renderer,
                       SDL_Window *window, const char *path)
{
    int w, h, pw, ph;
    SDL_GetWindowSize(window, &w, &h); SDL_GetRendererOutputSize(renderer, &pw, &ph);
    float scale = w > 0 ? (float)pw / (float)w : 1;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    if (ui->normal) {
        SDL_DestroyTexture(sdl.ogl.font_tex); sdl.ogl.font_tex = NULL;
        nk_font_atlas_clear(&sdl.atlas);
    }
    ui->font_scale = scale;
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    static const nk_rune ranges[] = {0x20, 0x17f, 0x2000, 0x206f, 0};
    struct nk_font_config cfg = nk_font_config(17 * scale);
    cfg.range = ranges; cfg.pixel_snap = 1; cfg.oversample_h = 1; cfg.oversample_v = 1;
    ui->normal = nk_font_atlas_add_from_file(atlas, path, roundf(17 * scale), &cfg);
    ui->heading = nk_font_atlas_add_from_file(atlas, path, roundf(20 * scale), &cfg);
    ui->code_font = nk_font_atlas_add_from_file(atlas,
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf", roundf(17 * scale), &cfg);
    if (!ui->normal) ui->normal = nk_font_atlas_add_default(atlas, roundf(17 * scale), &cfg);
    if (!ui->normal) { fputs("Cannot load a font.\n", stderr); exit(1); }
    if (!ui->heading) ui->heading = ui->normal;
    if (!ui->code_font) ui->code_font = ui->normal;
    ui->normal->handle.height = 17; ui->heading->handle.height = 20; ui->code_font->handle.height = 17;
    nk_sdl_font_stash_end();
    SDL_SetTextureScaleMode(sdl.ogl.font_tex, SDL_ScaleModeNearest);
    nk_style_set_font(ctx, &ui->normal->handle);
}

int main(int argc, char **argv)
{
    const char *local_model = "";
    const char *projects_path=".aimt-projects",*add_project=NULL;
    const char *project_path = ".", *plugins_path = AIMT_DEFAULT_PLUGINS;
    const char *shot_path = NULL, *catalog_path = AIMT_DEFAULT_CATALOG, *settings_path = AIMT_DEFAULT_SETTINGS;
    const char *font_path = "/usr/share/fonts/liberation/LiberationSans-Regular.ttf";
    int frames = 0, initial_tab = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) project_path = argv[++i];
        else if (strcmp(argv[i], "--projects") == 0 && i + 1 < argc) projects_path = argv[++i];
        else if (strcmp(argv[i], "--add-project") == 0 && i + 1 < argc) add_project = argv[++i];
        else if (strcmp(argv[i], "--local-model") == 0 && i + 1 < argc) local_model = argv[++i];
        else if (strcmp(argv[i], "--plugins") == 0 && i + 1 < argc) plugins_path = argv[++i];
        else if (strcmp(argv[i], "--catalog") == 0 && i + 1 < argc) catalog_path = argv[++i];
        else if (strcmp(argv[i], "--settings") == 0 && i + 1 < argc) settings_path = argv[++i];
        else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) font_path = argv[++i];
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) shot_path = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end = NULL; long n = strtol(argv[++i], &end, 10);
            if (!end || *end || n < 1 || n > 10000) return 2;
            frames = (int)n;
        } else if (strcmp(argv[i], "--recommendations-view") == 0) initial_tab = 4;
        else if (strcmp(argv[i], "--models-view") == 0) initial_tab = 1;
        else if (strcmp(argv[i], "--safe") == 0) {
            fputs("Safe Mode is not implemented. Startup refused.\n", stderr); return 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            puts("ai-multitool-agents-manager-gui [--project PATH] [--plugins PATH] [--font TTF] [--catalog JSON] [--settings DIR] [--local-model MODEL]");
            puts("Projects: --projects PRIVATE_DIR --add-project PATH");
            puts("Development: --frames N --screenshot FILE.bmp --models-view");
            return 0;
        } else { fputs("Unknown argument. Use --help.\n", stderr); return 2; }
    }
    if(strlen(local_model)>=256)return 2;
    desktop *ui = calloc(1, sizeof(*ui));
    if (!ui) return 1;
    snprintf(ui->local_model,sizeof(ui->local_model),"%s",local_model);
    ui->codex = aimt_codex_create();
    if (!ui->codex) { free(ui); return 1; }
    ui->project.root_fd = -1;
    ui->projects_path=projects_path;
    aimt_project_book_load(projects_path,&ui->projects);
    if(add_project)aimt_project_book_add(&ui->projects,add_project);
    ui->picker.root_fd = -1;
    aimt_config config = aimt_config_default();
    if (aimt_app_create(&config, &ui->app) != AIMT_OK || !aimt_plugins_load(plugins_path, &ui->registry)) {
        fputs("Cannot load main module or plugin manifests.\n", stderr);
        aimt_app_destroy(ui->app); aimt_local_destroy(ui->local_inference); aimt_codex_destroy(ui->codex); free(ui); return 1;
    }
    ui->settings_path = settings_path;
    for (size_t i = 0; i < ui->registry.count; ++i) aimt_plugin_settings_load(settings_path, &ui->registry.plugins[i]);
    aimt_catalog_load(catalog_path, &ui->catalog);
    choose_plugin(ui, 0); ui->show_settings = true; ui->settings_tab = initial_tab;
    ui->policy = aimt_policy_default();
    snprintf(ui->project_input, sizeof(ui->project_input), "%s", project_path);
    if (aimt_project_open(&ui->project, project_path)) refresh_files(ui);
    if(ui->project.root_fd>=0 && aimt_project_book_add(&ui->projects,ui->project.root))aimt_project_book_save(projects_path,&ui->projects);
    snprintf(ui->ollama, sizeof(ui->ollama), "http://127.0.0.1:11434");
    const char *environment_endpoint = getenv("OLLAMA_HOST");
    if (environment_endpoint && aimt_local_endpoint_valid(environment_endpoint))
        snprintf(ui->ollama, sizeof(ui->ollama), "%s", environment_endpoint);
    snprintf(ui->llama, sizeof(ui->llama), "http://127.0.0.1:8080");
    set_notice(ui, "Specialized agents - local workspace - module settings");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Cannot initialize desktop: %s\n", SDL_GetError());
        aimt_project_close(&ui->project); aimt_app_destroy(ui->app); aimt_local_destroy(ui->local_inference); aimt_codex_destroy(ui->codex); free(ui); SDL_Quit(); return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *window = SDL_CreateWindow("AI Multitool Agents Manager", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        1440, 940, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!window || !renderer) {
        fprintf(stderr, "Cannot create window: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        aimt_project_close(&ui->project); aimt_app_destroy(ui->app); aimt_local_destroy(ui->local_inference); aimt_codex_destroy(ui->codex); free(ui);
        curl_global_cleanup(); SDL_Quit(); return 1;
    }
    SDL_SetWindowMinimumSize(window, 1100, 760);
    struct nk_context *ctx = nk_sdl_init(window, renderer);
    load_fonts(ctx, ui, renderer, window, font_path); theme(ctx);
    SDL_StartTextInput(); start_discovery(ui);
    bool running = true;
    int rendered = 0, result = 0;
    while (running) {
        if (ui->terminal_pid > 0) {
            int terminal_status = 0;
            pid_t finished = waitpid(ui->terminal_pid, &terminal_status, WNOHANG);
            if (finished > 0 || (finished < 0 && errno == ECHILD)) {
                ui->terminal_pid = 0;
                if (finished > 0 && (!WIFEXITED(terminal_status) || WEXITSTATUS(terminal_status) != 0))
                    set_notice(ui, "The terminal exited with an error. Check graphical session availability.");
            }
        }
        SDL_Event event;
        nk_input_begin(ctx);
        if (SDL_WaitEventTimeout(&event, frames ? 16 : 80)) {
            do {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q &&
                    (event.key.keysym.mod & KMOD_CTRL)) running = false;
                nk_sdl_handle_event(&event);
                /* Preserve mouse transitions and bound per-frame text input.
                 * Regular character key events can be batched without rendering
                 * three frames per typed character. */
                if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                    (event.type == SDL_TEXTINPUT && ctx->input.keyboard.text_len > NK_INPUT_MAX - 32) ||
                    ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
                     (event.key.keysym.sym < SDLK_SPACE || event.key.keysym.sym >= SDLK_CAPSLOCK))) break;
            } while (SDL_PollEvent(&event));
        }
        nk_input_end(ctx); poll_discovery(ui);
        ui->codex_was_busy=ui->codex_was_busy || aimt_codex_get_status(ui->codex)->busy;
        aimt_codex_poll(ui->codex);
        if (ui->local_inference && aimt_local_done(ui->local_inference)) {
            bool success=false;
            const char *answer=aimt_local_result(ui->local_inference,&success);
            snprintf(ui->local_answer,sizeof(ui->local_answer),"%s",answer);
            set_notice(ui,success?"The local model completed the task.":answer);
            aimt_local_destroy(ui->local_inference);ui->local_inference=NULL;
        }
        const aimt_codex_status *codex_state = aimt_codex_get_status(ui->codex);
        if(ui->codex_was_busy && !codex_state->busy && codex_state->task_failed && ui->active_task[0]) {
            if(!local_coordinator(ui,ui->active_task,ui->execution_mode==3 || ui->execution_mode==4))
                snprintf(ui->message,sizeof(ui->message),"%s",ui->active_task);
            ui->active_task[0]=0;
        }
        if (ui->codex_was_busy && !codex_state->busy) refresh_files(ui);
        ui->codex_was_busy = codex_state->busy;
        ui->policy.cloud_connected = codex_state->authenticated;
        if (strcmp(ui->codex_notice, codex_state->status)) {
            snprintf(ui->codex_notice, sizeof(ui->codex_notice), "%s", codex_state->status);
            if (ui->use_codex) set_notice(ui, codex_state->status);
        }
        int width, height; SDL_GetWindowSize(window, &width, &height);
        int pixel_width, pixel_height;
        SDL_GetRendererOutputSize(renderer, &pixel_width, &pixel_height);
        float scale = width > 0 ? (float)pixel_width / (float)width : 1;
        if (fabsf(scale - ui->font_scale) > 0.05f) load_fonts(ctx, ui, renderer, window, font_path);
        SDL_RenderSetScale(renderer, scale, height > 0 ? (float)pixel_height / (float)height : 1);
        draw_header(ctx, ui, width); draw_modules(ctx, ui, width); draw_sidebar(ctx, ui, height);
        draw_workspace(ctx, ui, width, height); draw_settings(ctx, ui, width, height);
        struct nk_vec2 saved = ctx->style.window.padding;
        ctx->style.window.padding = nk_vec2(8, 3);
        draw_footer(ctx, ui, width, height); ctx->style.window.padding = saved;
        SDL_SetRenderDrawColor(renderer, 12, 17, 24, 255); SDL_RenderClear(renderer);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        rendered++;
        if (!running || (frames && rendered >= frames)) {
            if (shot_path && !screenshot(renderer, pixel_width, pixel_height, shot_path)) result = 1;
            running = false;
        }
        SDL_RenderPresent(renderer);
    }
    if (ui->job.thread) { SDL_AtomicSet(&ui->job.cancel_pull, 1); SDL_WaitThread(ui->job.thread, NULL); ui->discovery = ui->job.result; }
    SDL_StopTextInput(); nk_sdl_shutdown(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    aimt_project_close(&ui->picker);
    aimt_project_close(&ui->project); aimt_app_destroy(ui->app); aimt_local_destroy(ui->local_inference); aimt_codex_destroy(ui->codex); free(ui);
    curl_global_cleanup(); SDL_Quit();
    return result;
}
