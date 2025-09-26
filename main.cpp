// main.cpp
// Productivity Tracker - Final consolidated version
// - Finalized UI: dark gray/black theme, logs+tasks layout, break controls, inline daily/weekly status.
// - Exports: hourly (today), weekly (last 7 days) with LLM-friendly JSONL hourly section.
// - Hourly popup plays cross-platform alert sound (best-effort) and is opened one-shot.
// - All helper functions, forward declarations and definitions included; guarded IMGUI loader macro.
// - Writes persistent files to ~/.productivity_tracker by default.
//
// Build: link with glad, glfw, imgui (with backends). Run from Terminal.

#if !defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <atomic>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
#endif

// --------------------------- Forward declarations --------------------------
static void add_random_break();
static void start_break(const std::string &type);
static void end_last_break_of_type(const std::string &type);
static void add_task(const std::string &name, int parent_idx);
static void append_daily_log(const char* type, const std::string &text);
static std::string export_text_to_file(const char* prefix, const char* content);
static std::string export_hourly_logs_today();
static std::string export_weekly_logs_file();
static void save_daily_status_to_disk_and_log(const std::string &text);
static void save_weekly_status_to_disk_and_log(const std::string &text);
static void save_tasks();

// ----------------------- Cross-platform alert (best-effort) ----------------
static void play_alert_sound() {
#ifdef _WIN32
    // Use MessageBeep for a quick attention sound
    for (int i = 0; i < 2; ++i) {
        MessageBeep(MB_ICONEXCLAMATION);
        Sleep(160);
    }
#elif defined(__APPLE__)
    // macOS: afplay is typically available
    system("afplay /System/Library/Sounds/Glass.aiff >/dev/null 2>&1 &");
#else
    // Linux: try a set of common players, fall back to BEL
    if (system("which paplay >/dev/null 2>&1") == 0) {
        system("paplay /usr/share/sounds/freedesktop/stereo/bell.oga >/dev/null 2>&1 &");
    } else if (system("which aplay >/dev/null 2>&1") == 0) {
        system("aplay /usr/share/sounds/alsa/Front_Center.wav >/dev/null 2>&1 &");
    } else if (system("which play >/dev/null 2>&1") == 0) {
        system("play -q /usr/share/sounds/alsa/Noise.wav >/dev/null 2>&1 &");
    } else {
        // Last resort: ASCII BEL (may be quiet or ignored)
        std::cout << '\a' << std::flush;
    }
#endif
}
static void play_alert_async() {
    std::thread([](){
        play_alert_sound();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        play_alert_sound();
    }).detach();
}

// ----------------------- App constants & state ----------------------------
static const char* APP_TITLE = "Productivity Tracker";
static const char* DATA_DIR_NAME = ".productivity_tracker";

static char hourlyInputText[256] = "";
static char dailyStatusText[512] = "";
static char weeklyStatusText[512] = "";
static char newTaskText[128] = "";

static bool requestHourlyPopup = false; // one-shot flag to open hourly popup safely

struct DailyLog {
    time_t ts;
    std::string type; // "HOURLY", "DAILY_STATUS", "WEEKLY_STATUS", ...
    std::string text;
};

struct BreakEntry { std::string type; time_t start = 0; time_t end = 0; };
struct Task { std::string name; int parent = -1; bool done = false; };

static std::vector<DailyLog> dailyLogs;
static std::vector<BreakEntry> breaks;
static std::vector<Task> tasks;
static int new_task_parent_idx = -1;

static std::mt19937 rng((unsigned)std::time(nullptr));

static const std::vector<std::string> kBreakTypes = {"Coffee", "Bathroom", "Water", "Lunch", "Stretch"};
static int selectedBreakTypeIndex = 0;

// ----------------------- File/time helpers --------------------------------
static std::string user_data_dir() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home) return std::string(".");
    return std::string(home) + "/" + DATA_DIR_NAME;
}
static bool ensure_dir_exists(const std::string &dir) {
#ifdef _WIN32
    int r = _mkdir(dir.c_str());
    return (r == 0) || (errno == EEXIST);
#else
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(dir.c_str(), 0700) == 0;
#endif
}
static std::string path_in_data(const char* filename) {
    std::string dir = user_data_dir();
    if (!ensure_dir_exists(dir)) return std::string(filename);
    return dir + "/" + filename;
}
static std::string format_time_local(time_t t) {
    if (t == 0) return std::string("(n/a)");
    struct tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}
static std::string format_iso_time(time_t t) {
    if (t == 0) return std::string("");
    struct tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}
static time_t parse_timestamp(const std::string &s) {
    struct tm tm{};
    int y,m,d,hh,mm,ss;
    if (sscanf(s.c_str(), "%4d-%2d-%2d %2d:%2d:%2d", &y,&m,&d,&hh,&mm,&ss) == 6) {
        tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
        tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = ss; tm.tm_isdst = -1;
        time_t t = mktime(&tm);
        if (t != (time_t)-1) return t;
    }
    return time(nullptr);
}
static std::string human_log_line(const char* type, const std::string &text, time_t ts = 0) {
    time_t t = ts ? ts : time(nullptr);
    std::ostringstream oss;
    oss << format_time_local(t) << " - " << type << " - " << text;
    return oss.str();
}
static bool append_line_to_file(const std::string &path, const std::string &line) {
    std::ofstream f(path, std::ios::app);
    if (!f) return false;
    f << line << "\n";
    return true;
}
static std::string export_text_to_file(const char* prefix, const char* content) {
    time_t now = time(nullptr);
    struct tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);
    std::ostringstream filename;
    filename << prefix << "_" << ts << ".txt";
    std::string path = path_in_data(filename.str().c_str());
    std::ofstream f(path);
    if (!f) return std::string();
    f << content << "\n";
    return path;
}

// ----------------------- Exports: hourly/weekly ---------------------------
static bool is_same_local_day(time_t a, time_t b) {
    struct tm ta{}, tb{};
#if defined(_WIN32)
    localtime_s(&ta, &a);
    localtime_s(&tb, &b);
#else
    localtime_r(&a, &ta);
    localtime_r(&b, &tb);
#endif
    return (ta.tm_year==tb.tm_year && ta.tm_mon==tb.tm_mon && ta.tm_mday==tb.tm_mday);
}
static std::string export_hourly_logs_today() {
    if (dailyLogs.empty()) return std::string();
    time_t now = time(nullptr);
    std::ostringstream content;
    for (const auto &d : dailyLogs) {
        if (d.type == "HOURLY" && is_same_local_day(now, d.ts)) {
            content << human_log_line(d.type.c_str(), d.text, d.ts) << "\n";
        }
    }
    if (content.str().empty()) return std::string();
    return export_text_to_file("hourly_logs_today", content.str().c_str());
}
static std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}
static std::string export_weekly_logs_file() {
    if (dailyLogs.empty()) return std::string();

    time_t now = time(nullptr);
    const time_t week_seconds = 7 * 24 * 60 * 60;
    time_t cutoff = now - week_seconds;

    std::ostringstream human_section;
    std::ostringstream hourly_jsonl_section;

    human_section << "WEEKLY LOG EXPORT\n";
    human_section << "Generated: " << format_time_local(now) << "\n";
    human_section << "Range: last 7 days\n\n";

    for (const auto &d : dailyLogs) {
        if (d.ts < cutoff) continue;
        human_section << human_log_line(d.type.c_str(), d.text, d.ts) << "\n";
        if (d.type == "HOURLY") {
            std::string iso = format_iso_time(d.ts);
            std::string js = std::string("{\"type\":\"HOURLY\",\"timestamp\":\"") + json_escape(iso) + "\",\"text\":\"" + json_escape(d.text) + "\"}";
            hourly_jsonl_section << js << "\n";
        }
    }

    std::ostringstream final_content;
    final_content << human_section.str();
    final_content << "\n=== HOURLY_ENTRIES_JSONL (one JSON object per line) ===\n";
    final_content << hourly_jsonl_section.str();
    final_content << "\n=== END OF EXPORT ===\n";

    std::string path = export_text_to_file("weekly_logs_export", final_content.str().c_str());
    return path;
}

// ----------------------- Persistence & data --------------------------------
static void append_daily_log(const char* type, const std::string &text) {
    DailyLog d{ time(nullptr), type, text };
    dailyLogs.push_back(d);
    append_line_to_file(path_in_data("daily_logs.txt"), human_log_line(type, text, d.ts));
}
static void save_tasks() {
    std::string p = path_in_data("tasks.txt");
    std::ofstream f(p);
    if (!f) return;
    for (size_t i = 0; i < tasks.size(); ++i) {
        f << i << ": [" << (tasks[i].done ? "x" : " ") << "] " << tasks[i].name;
        if (tasks[i].parent != -1) f << " (parent=" << tasks[i].parent << ")";
        f << "\n";
    }
}
static void save_daily_status_to_disk_and_log(const std::string &text) {
    append_line_to_file(path_in_data("daily_status.txt"), human_log_line("DAILY_STATUS", text));
    append_daily_log("DAILY_STATUS", text);
    std::string p = export_text_to_file("daily_status_saved", text.c_str());
    if (!p.empty()) append_daily_log("EXPORT", std::string("Exported daily status to ") + p);
}
static void save_weekly_status_to_disk_and_log(const std::string &text) {
    append_line_to_file(path_in_data("weekly_status.txt"), human_log_line("WEEKLY_STATUS", text));
    append_daily_log("WEEKLY_STATUS", text);
    std::string p = export_text_to_file("weekly_status_saved", text.c_str());
    if (!p.empty()) append_daily_log("EXPORT", std::string("Exported weekly status to ") + p);
}
static void load_tasks() {
    tasks.clear();
    std::ifstream f(path_in_data("tasks.txt"));
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t colon = line.find(':');
        std::string rest = (colon == std::string::npos) ? line : line.substr(colon + 1);
        size_t pos = rest.find_first_not_of(" \t");
        if (pos != std::string::npos) rest = rest.substr(pos);
        bool done = false;
        if (rest.size() >= 3 && rest[0] == '[' && rest[2] == ']') {
            done = (rest[1] == 'x' || rest[1] == 'X');
            size_t br = rest.find(']');
            if (br != std::string::npos) rest = rest.substr(br + 1);
            pos = rest.find_first_not_of(" \t");
            if (pos != std::string::npos) rest = rest.substr(pos);
        }
        int parent = -1;
        size_t ppos = rest.rfind("(parent=");
        if (ppos != std::string::npos) {
            size_t endp = rest.find(')', ppos);
            if (endp != std::string::npos) {
                std::string num = rest.substr(ppos + 8, endp - (ppos + 8));
                try { parent = std::stoi(num); } catch(...) { parent = -1; }
                rest = rest.substr(0, ppos);
                while (!rest.empty() && isspace((unsigned char)rest.back())) rest.pop_back();
            }
        }
        Task t; t.name = rest; t.parent = parent; t.done = done;
        tasks.push_back(t);
    }
}
static void load_daily_logs() {
    dailyLogs.clear();
    std::ifstream f(path_in_data("daily_logs.txt"));
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t firstDash = line.find(" - ");
        if (firstDash != std::string::npos) {
            size_t secondDash = line.find(" - ", firstDash + 3);
            DailyLog d;
            if (secondDash != std::string::npos) {
                std::string ts = line.substr(0, firstDash);
                d.ts = parse_timestamp(ts);
                d.type = line.substr(firstDash + 3, secondDash - (firstDash + 3));
                size_t restStart = secondDash + 3;
                d.text = (restStart < line.size()) ? line.substr(restStart) : std::string();
            } else {
                std::string ts = line.substr(0, firstDash);
                d.ts = parse_timestamp(ts);
                d.type = "LOG";
                d.text = line.substr(firstDash + 3);
            }
            dailyLogs.push_back(d);
        } else {
            DailyLog d; d.ts = time(nullptr); d.type = "LOG"; d.text = line;
            dailyLogs.push_back(d);
        }
    }
}

// ---------------- Breaks/tasks helper definitions -------------------------
static void start_break(const std::string &type) {
    BreakEntry b; b.type = type; b.start = time(nullptr); b.end = 0;
    breaks.push_back(b);
    append_daily_log("BREAK_START", std::string("Started break: ") + type);
}
static void end_last_break_of_type(const std::string &type) {
    for (auto it = breaks.rbegin(); it != breaks.rend(); ++it) {
        if (it->type == type && it->end == 0) {
            it->end = time(nullptr);
            std::ostringstream oss;
            oss << "Ended break: " << it->type << " (start " << format_time_local(it->start)
                << ", end " << format_time_local(it->end) << ")";
            append_daily_log("BREAK_END", oss.str());
            return;
        }
    }
    append_daily_log("BREAK_WARN", std::string("Tried to end break but none active: ") + type);
}
static void add_random_break() {
    std::uniform_int_distribution<int> dtype(0, (int)kBreakTypes.size()-1);
    std::uniform_int_distribution<int> dmin(1, 20);
    std::string t = kBreakTypes[dtype(rng)];
    BreakEntry b; b.type = t; b.start = time(nullptr) - dmin(rng)*60; b.end = time(nullptr);
    breaks.push_back(b);
    std::ostringstream oss; oss << "Random break: " << b.type << " (" << format_time_local(b.start) << " - " << format_time_local(b.end) << ")";
    append_daily_log("BREAK_RANDOM", oss.str());
}
static void add_task(const std::string &name, int parent_idx) {
    Task tt; tt.name = name; tt.parent = parent_idx; tt.done = false;
    tasks.push_back(tt);
    save_tasks();
    append_daily_log("TASK", std::string("Added task: ") + name);
}

static void clearAllData()
{
    // Clear in-memory structures
    dailyLogs.clear();
    breaks.clear();
    tasks.clear();
}

// ----------------------- ImGui theme & helpers ----------------------------
static void ApplyGrayTheme() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    ImVec4 bg     = ImVec4(0.05f,0.05f,0.06f,1.0f);
    ImVec4 bgAlt  = ImVec4(0.09f,0.09f,0.10f,1.0f);
    ImVec4 pane   = ImVec4(0.12f,0.12f,0.13f,1.0f);
    ImVec4 text   = ImVec4(0.92f,0.92f,0.93f,1.0f);
    ImVec4 accent = ImVec4(0.20f,0.55f,0.90f,1.0f);
    ImVec4 border = ImVec4(0.13f,0.13f,0.14f,1.0f);

    style.Colors[ImGuiCol_WindowBg]             = bg;
    style.Colors[ImGuiCol_ChildBg]              = bgAlt;
    style.Colors[ImGuiCol_FrameBg]              = pane;
    style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f,0.18f,0.2f,1.0f);
    style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.22f,0.22f,0.24f,1.0f);
    style.Colors[ImGuiCol_TitleBg]              = bg;
    style.Colors[ImGuiCol_TitleBgActive]        = bg;
    style.Colors[ImGuiCol_MenuBarBg]            = bgAlt;
    style.Colors[ImGuiCol_Header]               = pane;
    style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.16f,0.16f,0.18f,1.0f);
    style.Colors[ImGuiCol_Button]               = ImVec4(0.14f,0.14f,0.16f,1.0f);
    style.Colors[ImGuiCol_ButtonHovered]        = accent;
    style.Colors[ImGuiCol_ButtonActive]         = ImVec4(accent.x*0.9f, accent.y*0.9f, accent.z*0.9f, 1.0f);
    style.Colors[ImGuiCol_Text]                 = text;
    style.Colors[ImGuiCol_TextDisabled]         = ImVec4(0.6f,0.6f,0.62f,1.0f);
    style.Colors[ImGuiCol_Border]               = border;
    style.Colors[ImGuiCol_ScrollbarBg]          = bgAlt;
    style.Colors[ImGuiCol_ScrollbarGrab]        = pane;
    style.Colors[ImGuiCol_PopupBg]              = bgAlt;
}

// ----------------------- Forwarding callbacks -----------------------------
static GLFWmousebuttonfun prev_mousebutton_cb = nullptr;
static GLFWscrollfun prev_scroll_cb = nullptr;
static GLFWkeyfun prev_key_cb = nullptr;
static GLFWcharfun prev_char_cb = nullptr;
static GLFWcursorposfun prev_cursorpos_cb = nullptr;
static GLFWcursorenterfun prev_cursorenter_cb = nullptr;

static void forward_mousebutton_cb(GLFWwindow* w, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
    if (prev_mousebutton_cb) prev_mousebutton_cb(w, button, action, mods);
}
static void forward_scroll_cb(GLFWwindow* w, double xoffset, double yoffset) {
    ImGui_ImplGlfw_ScrollCallback(w, xoffset, yoffset);
    if (prev_scroll_cb) prev_scroll_cb(w, xoffset, yoffset);
}
static void forward_key_cb(GLFWwindow* w, int key, int scancode, int action, int mods) {
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
    if (prev_key_cb) prev_key_cb(w, key, scancode, action, mods);
}
static void forward_char_cb(GLFWwindow* w, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(w, c);
    if (prev_char_cb) prev_char_cb(w, c);
}
static void forward_cursorpos_cb(GLFWwindow* w, double xpos, double ypos) {
    ImGui_ImplGlfw_CursorPosCallback(w, xpos, ypos);
    if (prev_cursorpos_cb) prev_cursorpos_cb(w, xpos, ypos);
}
static void forward_cursorenter_cb(GLFWwindow* w, int entered) {
    ImGui_ImplGlfw_CursorEnterCallback(w, entered);
    if (prev_cursorenter_cb) prev_cursorenter_cb(w, entered);
}
static void InstallForwardingCallbacks(GLFWwindow* window) {
    prev_mousebutton_cb = glfwSetMouseButtonCallback(window, forward_mousebutton_cb);
    prev_scroll_cb = glfwSetScrollCallback(window, forward_scroll_cb);
    prev_key_cb = glfwSetKeyCallback(window, forward_key_cb);
    prev_char_cb = glfwSetCharCallback(window, forward_char_cb);
    prev_cursorpos_cb = glfwSetCursorPosCallback(window, forward_cursorpos_cb);
    prev_cursorenter_cb = glfwSetCursorEnterCallback(window, forward_cursorenter_cb);
}

// ----------------------- UI helpers --------------------------------------
static std::string makeButtonLabel(const std::string &visible, const std::string &uniqueId) {
    return visible + "###" + uniqueId;
}

static void drawTasksRecursive(int idx, int depth = 0)
{
    // Safety check
    if (idx < 0 || idx >= (int)tasks.size()) return;

    ImGui::PushID(idx);

    // Render checkbox first (acts like the first column).
    // Use a hidden visible label "##" to keep the UI clean; PushID makes it unique.
    bool done = tasks[idx].done;
    if (ImGui::Checkbox("##task_done", &done)) {
        tasks[idx].done = done;
        save_tasks();
        // optional logging:
        // appendDailyLog("TASK", std::string("Toggled task: ") + tasks[idx].name + (done ? " [done]" : " [not done]"));
    }

    // Place the tree node to the right of the checkbox.
    ImGui::SameLine();

    // Indent the tree node according to depth so arrows/labels line up correctly.
    // We indent the tree node only (not the checkbox) so the checkbox remains a
    // fixed "first column" while the tree arrow/label sits in the second column.
    const float indentPerLevel = 18.0f;
    if (depth > 0) ImGui::Indent(depth * indentPerLevel);

    // Determine if this node has children
    bool hasChild = false;
    for (int i = 0; i < (int)tasks.size(); ++i) {
        if (tasks[i].parent == idx) { hasChild = true; break; }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChild) flags |= ImGuiTreeNodeFlags_Leaf;

    // Draw the tree node label (arrow will appear atsave_tasks the indented position).
    bool opened = ImGui::TreeNodeEx((void*)(intptr_t)idx, flags, "%s", tasks[idx].name.c_str());

    // Unindent immediately after drawing the node so children are indented relative to the node.
    if (depth > 0) ImGui::Unindent(depth * indentPerLevel);

    // If expanded, recurse into children (children will render their own checkbox + indented node)
    if (opened) {
        for (int i = 0; i < (int)tasks.size(); ++i) {
            if (tasks[i].parent == idx) drawTasksRecursive(i, depth + 1);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}
// ----------------------- Main ---------------------------------------------
int main(int, char**) {
    if (!glfwInit()) { fprintf(stderr,"glfwInit failed\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

    const int initial_w = 1200, initial_h = 760;
    GLFWwindow* window = glfwCreateWindow(initial_w, initial_h, APP_TITLE, NULL, NULL);
    if (!window) { fprintf(stderr,"glfwCreateWindow failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwFocusWindow(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { fprintf(stderr,"gladLoadGLLoader failed\n"); glfwDestroyWindow(window); glfwTerminate(); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;

#ifdef ImGuiConfigFlags_DockingEnable
    io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
#endif
#ifdef ImGuiConfigFlags_ViewportsEnable
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
#endif
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyGrayTheme();

    const char* glsl_version = "#version 330";
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init(glsl_version);

    InstallForwardingCallbacks(window);

    io.Fonts->AddFontDefault();
    ImGui_ImplOpenGL3_CreateDeviceObjects();

    load_tasks();
    load_daily_logs();

    double lastTime = glfwGetTime();
    int lastHour = -1;

    // UI state for Clear confirmation
    bool showClearConfirm = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int display_w=1, display_h=1;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        double now = glfwGetTime();
        float delta = (float)(now - lastTime);
        if (delta <= 0.0f) delta = 1.0f/60.0f;
        lastTime = now;
        io.DisplaySize = ImVec2((float)display_w, (float)display_h);
        io.DeltaTime = delta;

        // hourly automatic trigger (one-shot)
        time_t ct = time(nullptr);
        struct tm cur_tm{};
#if defined(_WIN32)
        localtime_s(&cur_tm, &ct);
#else
        localtime_r(&ct, &cur_tm);
#endif
        int curHour = cur_tm.tm_hour;
        if (curHour != lastHour) { requestHourlyPopup = true; lastHour = curHour; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Open hourly popup if requested and play alert
        if (requestHourlyPopup) {
            ImGui::OpenPopup("Hourly Log");
            play_alert_async();
            requestHourlyPopup = false;
        }

        // Main window spanning the work area
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                                   | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
        ImGui::Begin("MainWindow", nullptr, mainFlags);

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Tasks")) save_tasks();
                if (ImGui::MenuItem("Save Daily Logs")) {
                    for (const auto &d : dailyLogs) append_line_to_file(path_in_data("daily_logs.txt"), human_log_line(d.type.c_str(), d.text, d.ts));
                }
                if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Toolbar (Test, Random, Exports, Clear All)
        

        // Top toolbar: Test hourly, Random break, Export statuses, Export weekly logs, Export hourly (today)
        // Test Hourly Popup (neutral)
        {
            ImVec4 btn = ImVec4(0.20f,0.20f,0.22f,1.0f);
            ImVec4 btnH = ImVec4(0.26f,0.26f,0.28f,1.0f);
            ImVec4 btnA = ImVec4(0.22f,0.22f,0.24f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, btn);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnA);
            if (ImGui::Button(makeButtonLabel("Test Hourly Popup", "btn_test_hourly_popup").c_str())) requestHourlyPopup = true;
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        // Random Break (red)
        {
            ImVec4 b = ImVec4(0.45f,0.20f,0.20f,1.0f);
            ImVec4 bh = ImVec4(0.70f,0.22f,0.22f,1.0f);
            ImVec4 ba = ImVec4(0.60f,0.18f,0.18f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, b);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ba);
            if (ImGui::Button(makeButtonLabel("Random Break", "btn_random_break").c_str())) add_random_break();
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        // Export Daily Status (yellow)
        {
            ImVec4 b = ImVec4(0.60f,0.55f,0.20f,1.0f);
            ImVec4 bh = ImVec4(0.85f,0.78f,0.22f,1.0f);
            ImVec4 ba = ImVec4(0.70f,0.65f,0.18f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, b);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ba);
            if (ImGui::Button(makeButtonLabel("Export Daily Status (file)", "btn_export_daily_status_top").c_str())) {
                std::string path = export_text_to_file("daily_status_export", dailyStatusText);
                if (!path.empty()) append_daily_log("EXPORT", std::string("Exported daily status to ") + path);
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        // Export Weekly Status (green)
        {
            ImVec4 b = ImVec4(0.20f,0.55f,0.30f,1.0f);
            ImVec4 bh = ImVec4(0.22f,0.78f,0.40f,1.0f);
            ImVec4 ba = ImVec4(0.18f,0.68f,0.28f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, b);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ba);
            if (ImGui::Button(makeButtonLabel("Export Weekly Status (file)", "btn_export_weekly_status_top").c_str())) {
                std::string path = export_text_to_file("weekly_status_export", weeklyStatusText);
                if (!path.empty()) append_daily_log("EXPORT", std::string("Exported weekly status to ") + path);
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        // Export Weekly Logs (purple) - includes daily status & hourly logs (LLM-friendly section)
        {
            ImVec4 b = ImVec4(0.45f,0.30f,0.6f,1.0f);
            ImVec4 bh = ImVec4(0.65f,0.38f,0.85f,1.0f);
            ImVec4 ba = ImVec4(0.55f,0.34f,0.72f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, b);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ba);
            if (ImGui::Button(makeButtonLabel("Export Weekly Logs (file)", "btn_export_weekly_logs_top").c_str())) {
                std::string p = export_weekly_logs_file();
                if (!p.empty()) append_daily_log("EXPORT", std::string("Exported weekly logs to ") + p);
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        // Export Hourly Logs (today) (blue)
        {
            ImVec4 b = ImVec4(0.2f,0.45f,0.85f,1.0f);
            ImVec4 bh = ImVec4(0.3f,0.6f,0.95f,1.0f);
            ImVec4 ba = ImVec4(0.18f,0.4f,0.8f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, b);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bh);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ba);
            if (ImGui::Button(makeButtonLabel("Export Hourly Logs (today)", "btn_export_hourly_today_top").c_str())) {
                std::string p = export_hourly_logs_today();
                if (!p.empty()) append_daily_log("EXPORT", std::string("Exported hourly logs (today) to ") + p);
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        {
            ImVec4 b  = ImVec4(0.7f, 0.12f, 0.12f, 1.0f);
            ImVec4 bh = ImVec4(0.9f, 0.18f, 0.18f, 1.0f);
            ImVec4 ba = ImVec4(0.8f, 0.14f, 0.14f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button,         b);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  bh);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ba);

            if (ImGui::Button("Clear All###btn_clear_all"))
                ImGui::OpenPopup("Confirm Clear All");

            // Always pop the three colors we pushed above (do this regardless of whether the button was pressed)
            ImGui::PopStyleColor(3);

            // Confirmation modal (opened by ImGui::OpenPopup above)
            if (ImGui::BeginPopupModal("Confirm Clear All", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                // When the window appears, set default focus on the "Yes" button for quicker keyboard confirmation
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                ImGui::TextWrapped(
                    "This will delete all persisted logs, tasks and status files and CLEAR in-memory data.\n\n"
                    "This action cannot be undone. Do you want to proceed?"
                );
                ImGui::Separator();

                // Confirm
                if (ImGui::Button("Yes - Clear All"))
                {
                    // clear in-memory + persisted state
                    clearAllData();

                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();

                // Cancel
                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }
        }
        ImGui::Separator();

        // Layout: left child (logs + tasks side-by-side) + right child (controls)
        float availW = ImGui::GetContentRegionAvail().x;
        float leftWidth = availW * 0.62f;
        float rightWidth = availW - leftWidth - ImGui::GetStyle().ItemSpacing.x;

        ImGui::BeginChild("left_panel", ImVec2(leftWidth, 0), true);
            float half = (leftWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::BeginChild("logs_col", ImVec2(half, 320), true, ImGuiWindowFlags_None);
                ImGui::Text("Daily Logs:");
                ImGui::Separator();
                ImGui::BeginChild("logs_list", ImVec2(0, -1), false, ImGuiWindowFlags_HorizontalScrollbar);
                    if (!dailyLogs.empty()) {
                        for (int i = (int)dailyLogs.size()-1; i>=0; --i) {
                            const DailyLog &d = dailyLogs[i];
                            ImVec4 col;
                            if (d.type == "HOURLY") col = ImVec4(0.4f,0.7f,1.0f,1.0f);
                            else if (d.type == "DAILY_STATUS") col = ImVec4(1.0f,0.9f,0.4f,1.0f);
                            else if (d.type == "WEEKLY_STATUS") col = ImVec4(0.6f,1.0f,0.6f,1.0f);
                            else if (d.type.rfind("BREAK",0)==0) col = ImVec4(1.0f,0.6f,0.6f,1.0f);
                            else if (d.type == "EXPORT") col = ImVec4(0.8f,0.6f,1.0f,1.0f);
                            else if (d.type == "TASK") col = ImVec4(0.8f,0.8f,0.85f,1.0f);
                            else col = ImVec4(0.9f,0.9f,0.9f,1.0f);
                            ImGui::PushStyleColor(ImGuiCol_Text, col);
                            ImGui::TextWrapped("%s", human_log_line(d.type.c_str(), d.text, d.ts).c_str());
                            ImGui::PopStyleColor();
                        }
                    } else {
                        ImGui::TextDisabled("(no logs yet)");
                    }
                ImGui::EndChild();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("tasks_col", ImVec2(half, 320), true, ImGuiWindowFlags_None);
            ImGui::Text("Tasks:");
            ImGui::Separator();
            ImGui::BeginChild("tasks_list", ImVec2(0, -1), false, ImGuiWindowFlags_None);
            if (!tasks.empty()) {
                for (int i = 0; i < (int)tasks.size(); ++i)
                    if (tasks[i].parent == -1)
                        drawTasksRecursive(i);
            } else {
                ImGui::TextDisabled("(no tasks)");
            }
            ImGui::EndChild();
            ImGui::EndChild();

            ImGui::Separator();

            ImGui::Text("Breaks:");
            if (!breaks.empty()) {
                if (ImGui::BeginTable("tbl_breaks_left", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("Start/End", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableHeadersRow();
                    for (int i=(int)breaks.size()-1;i>=0;--i) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(breaks[i].type.c_str());
                        ImGui::TableNextColumn();
                        if (breaks[i].start) {
                            if (breaks[i].end) ImGui::Text("%s -> %s", format_time_local(breaks[i].start).c_str(), format_time_local(breaks[i].end).c_str());
                            else ImGui::Text("%s (active)", format_time_local(breaks[i].start).c_str());
                        } else ImGui::TextUnformatted("(n/a)");
                        ImGui::TableNextColumn();
                        if (breaks[i].end == 0) {
                            char endLabel[64]; snprintf(endLabel, sizeof(endLabel), "End %d###btn_end_%d", i, i);
                            if (ImGui::Button(endLabel)) { breaks[i].end = time(nullptr); append_daily_log("BREAK_END", std::string("Ended break: ") + breaks[i].type); }
                        } else {
                            char startLabel[64]; snprintf(startLabel, sizeof(startLabel), "Start %d###btn_start_%d", i, i);
                            if (ImGui::Button(startLabel)) start_break(breaks[i].type);
                        }
                    }
                    ImGui::EndTable();
                }
            } else ImGui::TextDisabled("(no breaks yet)");
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("right_panel", ImVec2(rightWidth, 0), true);
            ImGui::Text("Break Controls:");
            ImGui::Separator();
            ImGui::SetNextItemWidth(180);
            if (ImGui::BeginCombo("Break Type###combo_break_types", kBreakTypes[selectedBreakTypeIndex].c_str())) {
                for (int i = 0; i < (int)kBreakTypes.size(); ++i) {
                    bool sel = (i == selectedBreakTypeIndex);
                    if (ImGui::Selectable(kBreakTypes[i].c_str(), sel)) selectedBreakTypeIndex = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Start Selected###btn_start_selected_right")) start_break(kBreakTypes[selectedBreakTypeIndex]);
            ImGui::SameLine();
            if (ImGui::Button("End Selected###btn_end_selected_right")) end_last_break_of_type(kBreakTypes[selectedBreakTypeIndex]);

            ImGui::Separator();
            ImGui::Text("Add Task:");
            ImGui::InputText("Task name###input_task_name_right", newTaskText, sizeof(newTaskText));
            std::vector<std::string> parentOptions;
            parentOptions.push_back("(none)");
            for (size_t i=0;i<tasks.size();++i) { std::ostringstream os; os<<i<<": "<<tasks[i].name; parentOptions.push_back(os.str()); }
            static int parentComboIndex = 0;
            ImGui::SetNextItemWidth(240);
            if (ImGui::BeginCombo("Parent###combo_parent_right", parentOptions[parentComboIndex].c_str())) {
                for (int n=0;n<(int)parentOptions.size();++n) {
                    bool sel = (n==parentComboIndex);
                    if (ImGui::Selectable(parentOptions[n].c_str(), sel)) { parentComboIndex = n; new_task_parent_idx = n - 1; }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Task###btn_add_task_right")) {
                if (std::strlen(newTaskText)>0) { add_task(std::string(newTaskText), new_task_parent_idx); std::memset(newTaskText,0,sizeof(newTaskText)); new_task_parent_idx=-1; }
            }

            ImGui::Separator();
            ImGui::Text("Quick Daily Log:");
            ImGui::InputText("Quick log###input_quick_log_right", hourlyInputText, sizeof(hourlyInputText));
            ImGui::SameLine();
            if (ImGui::Button("Log Now###btn_log_now_right")) {
                if (std::strlen(hourlyInputText)>0) { append_daily_log("HOURLY", std::string(hourlyInputText)); std::memset(hourlyInputText,0,sizeof(hourlyInputText)); }
            }

            ImGui::Separator();
            ImGui::Text("Daily Status (inline):");
            ImGui::InputTextMultiline("Daily status###daily_status_inline_right", dailyStatusText, sizeof(dailyStatusText), ImVec2(-1,100));
            ImGui::SameLine();
            if (ImGui::Button("Save Daily Status###btn_save_daily_inline_right")) {
                if (std::strlen(dailyStatusText)>0) { save_daily_status_to_disk_and_log(dailyStatusText); std::memset(dailyStatusText,0,sizeof(dailyStatusText)); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Export Daily Status (file)###btn_export_daily_status_right")) {
                std::string p = export_text_to_file("daily_status_export", dailyStatusText);
                if (!p.empty()) append_daily_log("EXPORT", std::string("Exported daily status to ") + p);
            }

            ImGui::Separator();
            ImGui::Text("Weekly Status (inline):");
            ImGui::InputTextMultiline("Weekly status###weekly_status_inline_right", weeklyStatusText, sizeof(weeklyStatusText), ImVec2(-1,140));
            ImGui::SameLine();
            if (ImGui::Button("Save Weekly Status###btn_save_weekly_inline_right")) {
                if (std::strlen(weeklyStatusText)>0) { save_weekly_status_to_disk_and_log(weeklyStatusText); std::memset(weeklyStatusText,0,sizeof(weeklyStatusText)); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Export Weekly Status (file)###btn_export_weekly_status_right")) {
                std::string p = export_text_to_file("weekly_status_export", weeklyStatusText);
                if (!p.empty()) append_daily_log("EXPORT", std::string("Exported weekly status to ") + p);
            }

            ImGui::Separator();
            if (ImGui::Button(makeButtonLabel("Export Hourly Logs (today)", "btn_export_hourly_today_right").c_str())) {
                std::string p = export_hourly_logs_today();
                if (!p.empty()) append_daily_log("EXPORT", std::string("Exported hourly logs (today) to ") + p);
            }

        ImGui::EndChild();

        ImGui::End(); // MainWindow

        // Hourly popup
        if (ImGui::BeginPopupModal("Hourly Log", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(0);
            ImGui::Text("What did you do this hour?");
            ImGui::InputText("##hourly_modal_input", hourlyInputText, sizeof(hourlyInputText));
            ImGui::Separator();
            if (ImGui::Button("Log###btn_modal_hourly_log")) {
                if (std::strlen(hourlyInputText) > 0) append_daily_log("HOURLY", std::string(hourlyInputText));
                std::memset(hourlyInputText, 0, sizeof(hourlyInputText));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Skip###btn_modal_hourly_skip")) {
                std::memset(hourlyInputText, 0, sizeof(hourlyInputText));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Render
        ImGui::Render();
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
