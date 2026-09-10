
#include "composition/client_helpers.h"

#if defined(__APPLE__)
#include <pthread/qos.h>
#endif
#include "openwow/net/adapters/diagnostics/packet_log.h"
#include "glue_host/glue_client.h"
#include "scenarios/scenario_runner.h"

#include "openwow/core/client_init.h"
#include "openwow/core/cvar.h"
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/login_resource_validator.h"
#include "openwow/debug/diagnostics/error_handler.h"
#include "openwow/core/fpu_control.h"
#include "openwow/platform/diagnostics/crash_handler.h"
#include "openwow/platform/adapters/sdl/platform_layer.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/platform/window/window_manager.h"
#include "openwow/platform/window/single_instance_guard.h"
#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/bootstrap/startup_trace.h"
#include "openwow/ui/glue/glue_lua_event_trace.h"

#include <SDL2/SDL.h>
#include "openwow/platform/window/sdl_syswm.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::filesystem::path AbsolutePathFromCurrentRoot(const std::filesystem::path& path) {
  std::error_code ec;
  auto absolute = std::filesystem::absolute(path, ec);
  if (ec) {
    return path;
  }
  return absolute.lexically_normal();
}

std::filesystem::path ExecutableDirectory() {
  const std::string module_dir = openwow::platform::OS_GetModuleDirectory();
  if (!module_dir.empty()) {
    return AbsolutePathFromCurrentRoot(std::filesystem::path(module_dir));
  }

  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path(".") : cwd;
}

bool HasDataSubdirectory(const std::filesystem::path& root) {
  std::error_code ec;
  return std::filesystem::is_directory(root / "Data", ec) && !ec;
}

std::filesystem::path BundleResourcesDirectory(
    const std::filesystem::path& exe_dir) {

  std::filesystem::path normalized_exe_dir = exe_dir.lexically_normal();
  if (normalized_exe_dir.filename().empty()) {
    normalized_exe_dir = normalized_exe_dir.parent_path();
  }
  if (normalized_exe_dir.filename() == "MacOS") {
    const std::filesystem::path contents_dir = normalized_exe_dir.parent_path();
    if (contents_dir.filename() == "Contents" &&
        contents_dir.parent_path().extension() == ".app") {
      return contents_dir / "Resources";
    }
  }

  if (normalized_exe_dir.extension() == ".app") {
    return normalized_exe_dir;
  }

  return {};
}

std::filesystem::path BundleSiblingDirectory(
    const std::filesystem::path& exe_dir) {
  const auto resources_dir = BundleResourcesDirectory(exe_dir);
  if (resources_dir.empty() || resources_dir.filename() != "Resources") {
    return {};
  }
  return resources_dir.parent_path().parent_path().parent_path();
}

std::filesystem::path ResolveContentRoot(
    const std::string& cli_game_data_path,
    const std::filesystem::path& user_data_root) {
  if (!cli_game_data_path.empty()) {
    return AbsolutePathFromCurrentRoot(std::filesystem::path(cli_game_data_path));
  }

  const char* env_game_data = std::getenv("OPENWOW_GAME_DATA");
  if (env_game_data != nullptr && env_game_data[0] != '\0') {
    return AbsolutePathFromCurrentRoot(std::filesystem::path(env_game_data));
  }

#if defined(OPENWOW_PLATFORM_IOS)
  const auto synchronized_content_root = user_data_root / "GameRoot";
  const auto synchronized_ready_marker =
      synchronized_content_root / ".openwow-ios-data-ready";
  std::error_code synchronized_marker_ec;
  const bool has_synchronized_marker = std::filesystem::is_regular_file(
      synchronized_ready_marker, synchronized_marker_ec);
  if (HasDataSubdirectory(synchronized_content_root) &&
      has_synchronized_marker && !synchronized_marker_ec) {
    return synchronized_content_root;
  }
  if (HasDataSubdirectory(synchronized_content_root) ||
      (has_synchronized_marker && !synchronized_marker_ec)) {
    std::cerr << "OpenWoW: ignoring incomplete synchronized iOS Data at "
              << synchronized_content_root.string()
              << "; run the openwow-ios-sync-data target to complete it\n";
  }
#else
  (void)user_data_root;
#endif

  const auto exe_dir = ExecutableDirectory();
  const auto bundle_resources = BundleResourcesDirectory(exe_dir);
  if (!bundle_resources.empty()) {
    const auto bundled_content_root = bundle_resources / "GameRoot";
    if (HasDataSubdirectory(bundled_content_root)) {
      return bundled_content_root;
    }
  }

  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);
  if (!ec) {
    cwd = AbsolutePathFromCurrentRoot(cwd);
    if (HasDataSubdirectory(cwd)) {
      return cwd;
    }
  }

  if (HasDataSubdirectory(exe_dir)) {
    return exe_dir;
  }

  const auto bundle_sibling = BundleSiblingDirectory(exe_dir);
  if (!bundle_sibling.empty() && HasDataSubdirectory(bundle_sibling)) {
    return bundle_sibling;
  }

#if defined(OPENWOW_DEVELOPMENT_CONTENT_ROOT)
  const auto development_content_root =
      AbsolutePathFromCurrentRoot(std::filesystem::path(OPENWOW_DEVELOPMENT_CONTENT_ROOT));
  if (HasDataSubdirectory(development_content_root)) {
    return development_content_root;
  }
#endif

  if (!cwd.empty()) {
    return cwd;
  }
  return exe_dir;
}

std::filesystem::path ResolveOverrideContentRoot() {
  const char* env_assets = std::getenv("OPENWOW_ENHANCED_ASSETS");
  if (env_assets != nullptr && env_assets[0] != '\0') {
    return AbsolutePathFromCurrentRoot(std::filesystem::path(env_assets));
  }

  const auto bundle_resources = BundleResourcesDirectory(ExecutableDirectory());
  if (!bundle_resources.empty()) {
    const auto bundled_override_root = bundle_resources / "OpenWoWOverrides";
    std::error_code ec;
    if (std::filesystem::is_directory(bundled_override_root, ec) && !ec) {
      return bundled_override_root;
    }
  }

#if defined(OPENWOW_DEVELOPMENT_OVERRIDE_ROOT)
  const auto development_override_root =
      AbsolutePathFromCurrentRoot(std::filesystem::path(OPENWOW_DEVELOPMENT_OVERRIDE_ROOT));
  std::error_code ec;
  if (std::filesystem::is_directory(development_override_root, ec) && !ec) {
    return development_override_root;
  }
#endif

  return {};
}

std::filesystem::path ResolveUserDataRoot() {
  const char* env_user_data = std::getenv("OPENWOW_USER_DATA");
  if (env_user_data != nullptr && env_user_data[0] != '\0') {
    return AbsolutePathFromCurrentRoot(std::filesystem::path(env_user_data));
  }

  const std::string platform_user_data =
      openwow::core::PlatformLayer::GetUserDataPath();
  if (!platform_user_data.empty()) {
    return AbsolutePathFromCurrentRoot(std::filesystem::path(platform_user_data));
  }

  return AbsolutePathFromCurrentRoot(ExecutableDirectory() / "OpenWoWUserData");
}

bool PrepareUserDataRoot(const std::filesystem::path& user_data_root) {
  const std::filesystem::path required_directories[] = {
      user_data_root,
      user_data_root / "ContentOverrides",
  };
  for (const auto& directory : required_directories) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
      std::cerr << "OpenWoW: failed to create user data directory "
                << directory.string() << ": " << ec.message() << '\n';
      return false;
    }
    if (!std::filesystem::is_directory(directory, ec) || ec) {
      std::cerr << "OpenWoW: user data path is not a directory: "
                << directory.string();
      if (ec) {
        std::cerr << ": " << ec.message();
      }
      std::cerr << '\n';
      return false;
    }
  }
  return true;
}

class ScenarioProfileIsolation {
 public:
  bool Prepare(const std::filesystem::path& artifacts_root,
               const std::vector<std::string>& extra_cvar_lines = {}) {
    root_ = (artifacts_root / ".openwow-scenario-profile").lexically_normal();

    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
    ec.clear();
    std::filesystem::create_directories(root_ / "WTF", ec);
    if (ec) {
      return false;
    }

    std::filesystem::permissions(
        root_, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace, ec);
    ec.clear();
    std::filesystem::permissions(
        root_ / "WTF", std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace, ec);

    static constexpr char kScenarioConfig[] =
        "SET hwDetect \"0\"\n"
        "SET movie \"0\"\n"
        "SET expansionMovie \"0\"\n"
        "SET readTOS \"1\"\n"
        "SET readEULA \"1\"\n"
        "SET readScanning \"1\"\n"
        "SET readContest \"1\"\n"
        "SET readTerminationWithoutNotice \"1\"\n"
        "SET converted \"1\"\n"
        "SET gxWindow \"1\"\n"
        "SET gxResolution \"1280x720\"\n";

    std::string config_contents(kScenarioConfig);
    for (const std::string& line : extra_cvar_lines) {
      config_contents += line;
    }

    const auto config_path = root_ / "WTF" / "Config.wtf";
    if (!openwow::platform::filesystem::AtomicWriteFile(config_path, config_contents)) {
      return false;
    }

    openwow::core::ida::CVar_SetConfigFileOverride(config_path.string());
    prepared_ = true;
    return true;
  }

  bool EnterClientWorkingDirectory() {
    if (!prepared_ || entered_) {
      return prepared_;
    }

    std::error_code ec;
    original_working_directory_ = std::filesystem::current_path(ec);
    if (ec) {
      return false;
    }
    std::filesystem::current_path(root_, ec);
    if (ec) {
      original_working_directory_.clear();
      return false;
    }
    original_writable_base_path_ =
        openwow::data::GetStartupFileSystemState().writable_base_path;
    openwow::data::SetStartupWritableBasePath(root_.string());
    entered_ = true;
    return true;
  }

  void LeaveClientWorkingDirectory() {
    if (!entered_) {
      return;
    }
    std::error_code ec;
    std::filesystem::current_path(original_working_directory_, ec);
    openwow::data::SetStartupWritableBasePath(original_writable_base_path_);
    entered_ = false;
  }

  ~ScenarioProfileIsolation() {
    LeaveClientWorkingDirectory();
    openwow::core::ida::CVar_SetConfigFileOverride({});
    if (!root_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(root_, ec);
    }
  }

 private:
  std::filesystem::path root_;
  std::filesystem::path original_working_directory_;
  std::string original_writable_base_path_;
  bool prepared_{false};
  bool entered_{false};
};

int RunClientProcess(int argc, char** argv) {
#if defined(__APPLE__)

  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
  std::optional<openwow::client::ScenarioOptions> scenario_opts;
  openwow::client::ScenarioOptions raw_scenario_opts;
  std::filesystem::path artifacts_dir = std::filesystem::path("artifacts");
  raw_scenario_opts.artifacts_dir = artifacts_dir;
  bool startup_trace_enabled = false;
  bool lua_trace_enabled = false;
  bool ui_frame_tree_dump_enabled = false;
  bool render_submit_trace_enabled = false;
  std::string cli_game_data_path;

  std::vector<std::string> scenario_extra_cvar_lines;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? std::string(argv[i]) : std::string();
    if (arg == "--scenario" && i + 1 < argc) {
      raw_scenario_opts.name = argv[++i] ? std::string(argv[i]) : std::string();
      continue;
    }
    if (arg == "--benchmark-scene" && i + 1 < argc) {
      raw_scenario_opts.benchmark_scene = argv[++i] ? std::string(argv[i]) : std::string();
      continue;
    }
    if (arg == "--benchmark-frames" && i + 1 < argc) {
      raw_scenario_opts.benchmark_frames =
          argv[++i] != nullptr ? std::atoi(argv[i]) : 0;
      continue;
    }
    if (arg == "--artifacts-dir" && i + 1 < argc) {
      artifacts_dir = std::filesystem::path(argv[++i] ? argv[i] : "");
      raw_scenario_opts.artifacts_dir = artifacts_dir;
      continue;
    }
    if (arg == "--game-data" && i + 1 < argc) {
      cli_game_data_path = argv[++i] ? std::string(argv[i]) : std::string();
      continue;
    }
    if (arg == "--set-cvar" && i + 1 < argc) {
      const std::string kv = argv[++i] ? std::string(argv[i]) : std::string();
      const auto eq = kv.find('=');
      if (eq != std::string::npos) {
        scenario_extra_cvar_lines.push_back(
            "SET " + kv.substr(0, eq) + " \"" + kv.substr(eq + 1) + "\"\n");
      } else {
        std::cerr << "OpenWoW: --set-cvar expects NAME=VALUE, got \"" << kv
                  << "\"\n";
      }
      continue;
    }
    if (arg == "--startup-trace") {
      startup_trace_enabled = true;
      continue;
    }
    if (arg == "--lua-trace") {
      lua_trace_enabled = true;
      continue;
    }
    if (arg == "--ui-frame-tree-dump") {
      ui_frame_tree_dump_enabled = true;
      continue;
    }
    if (arg == "--render-submit-trace") {
      render_submit_trace_enabled = true;
      continue;
    }
  }
  if (!startup_trace_enabled) {
    const char* v = std::getenv("OPENWOW_STARTUP_TRACE");
    if (v != nullptr) {
      std::string s(v);
      for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      startup_trace_enabled = (s == "1" || s == "true" || s == "yes" || s == "on");
    }
  }
  if (!raw_scenario_opts.name.empty()) {
    if (const char* account = std::getenv("OPENWOW_SCENARIO_ACCOUNT"); account != nullptr) {
      raw_scenario_opts.account = account;
    }
    if (const char* password = std::getenv("OPENWOW_SCENARIO_PASSWORD"); password != nullptr) {
      raw_scenario_opts.password = password;
    }
    scenario_opts = std::move(raw_scenario_opts);
  }

  if (!lua_trace_enabled) {
    const char* v = std::getenv("OPENWOW_LUA_TRACE");
    if (v != nullptr) {
      std::string s(v);
      for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      lua_trace_enabled = (s == "1" || s == "true" || s == "yes" || s == "on");
    }
  }
  if (!ui_frame_tree_dump_enabled) {
    const char* v = std::getenv("OPENWOW_UI_FRAME_TREE_DUMP");
    if (v != nullptr) {
      std::string s(v);
      for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      ui_frame_tree_dump_enabled = (s == "1" || s == "true" || s == "yes" || s == "on");
    }
  }
  if (!render_submit_trace_enabled) {
    const char* v = std::getenv("OPENWOW_RENDER_SUBMIT_TRACE");
    if (v != nullptr) {
      std::string s(v);
      for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      render_submit_trace_enabled = (s == "1" || s == "true" || s == "yes" || s == "on");
    }
  }

  std::optional<openwow::runtime::bootstrap::StartupTrace> startup_trace;
  if (startup_trace_enabled) {
    startup_trace.emplace();
    startup_trace->Add("process.entry");
  }
  bool startup_trace_written = false;
  const auto write_startup_trace = [&]() {
    if (startup_trace_written || !startup_trace.has_value()) return;
    (void)startup_trace->WriteTsvFile(artifacts_dir / "startup_trace.log");
    startup_trace_written = true;
  };

  std::optional<openwow::ui::glue::GlueLuaEventTrace> lua_trace;
  if (lua_trace_enabled) {
    lua_trace.emplace();
  }

  std::optional<std::filesystem::path> ui_frame_tree_dump_path;
  if (ui_frame_tree_dump_enabled) {
    ui_frame_tree_dump_path = artifacts_dir / "ui_frame_tree" / "sample_dump.tsv";
  }

  std::optional<openwow::render::RenderSubmitTrace> render_submit_trace;
  std::optional<std::filesystem::path> render_submit_trace_path;
  if (render_submit_trace_enabled) {
    render_submit_trace.emplace();
    render_submit_trace_path = artifacts_dir / "render_submit" / "sample_submit_trace.tsv";
  }
  std::optional<std::reference_wrapper<openwow::render::RenderSubmitTrace>>
      render_submit_trace_ref;
  if (render_submit_trace.has_value()) {
    render_submit_trace_ref = *render_submit_trace;
  }

  const auto user_data_root = ResolveUserDataRoot();
  const auto content_root = ResolveContentRoot(cli_game_data_path, user_data_root);
  const auto override_content_root = ResolveOverrideContentRoot();

  if (artifacts_dir.empty()) {
    artifacts_dir = user_data_root / "artifacts";
  } else if (artifacts_dir.is_relative()) {
    artifacts_dir = user_data_root / artifacts_dir;
  }
  artifacts_dir = artifacts_dir.lexically_normal();
  if (scenario_opts.has_value()) {
    scenario_opts->artifacts_dir = artifacts_dir;
  }
  if (ui_frame_tree_dump_path.has_value()) {
    ui_frame_tree_dump_path =
        artifacts_dir / "ui_frame_tree" / "sample_dump.tsv";
  }
  if (render_submit_trace_path.has_value()) {
    render_submit_trace_path =
        artifacts_dir / "render_submit" / "sample_submit_trace.tsv";
  }

  if (!PrepareUserDataRoot(user_data_root)) {
    write_startup_trace();
    return 1;
  }

  {
    std::error_code ec;
    std::filesystem::current_path(user_data_root, ec);
    if (ec) {
      std::cerr << "OpenWoW: failed to set working directory to "
                << user_data_root.string() << ": " << ec.message() << '\n';
      write_startup_trace();
      return 1;
    }
  }
  openwow::data::SetStartupExecutableBasePath(content_root.string());
  openwow::data::SetStartupWritableBasePath(user_data_root.string());

  std::optional<ScenarioProfileIsolation> scenario_profile;
  if (scenario_opts.has_value()) {
    scenario_profile.emplace();
    if (!scenario_profile->Prepare(artifacts_dir, scenario_extra_cvar_lines)) {
      std::cerr << "OpenWoW: failed to create isolated scenario profile under "
                << artifacts_dir.string() << '\n';
      write_startup_trace();
      return 1;
    }
  }

  openwow::platform::SingleInstanceGuard single_instance_guard;
  if (!scenario_opts.has_value() && !single_instance_guard.TryAcquire()) {
    std::cerr << "OpenWoW: Another instance is already running.\n";
    write_startup_trace();
    return 1;
  }

  openwow::platform::CrashHandler::Get().Install(
      openwow::platform::CrashContext{
          .build_version = "WotLK 3.3.5a OpenWoW",
          .logs_directory = (user_data_root / "logs").string(),
      });

  {
    const auto assert_log = (user_data_root / "Errors" / "Assert.log").string();
    openwow::debug::ErrorHandler::Get().SetAssertLogPath(assert_log);
  }

#if defined(__APPLE__) && !defined(OPENWOW_PLATFORM_IOS)
  (void)SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
#endif
#if defined(OPENWOW_PLATFORM_IOS)
  (void)SDL_SetHint("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight");
  (void)SDL_SetHint("SDL_IOS_HIDE_HOME_INDICATOR", "2");
  (void)SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  (void)SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
    if (startup_trace.has_value()) startup_trace->Add("platform.sdl_init.fail");
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    write_startup_trace();
    return 1;
  }
  if (startup_trace.has_value()) startup_trace->Add("platform.sdl_init");

  openwow::core::InitFPU();
  if (startup_trace.has_value()) startup_trace->Add("process.fpu_init");

  (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  (void)SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

  Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(OPENWOW_PLATFORM_IOS)
  window_flags |= SDL_WINDOW_FULLSCREEN;
#else
  window_flags |= SDL_WINDOW_RESIZABLE;
#endif
  SDL_Window* window = SDL_CreateWindow(
      "World of Warcraft", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
      window_flags);
  if (!window) {
    if (startup_trace.has_value()) startup_trace->Add("platform.window_create.fail");
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    write_startup_trace();
    return 1;
  }
  if (startup_trace.has_value()) startup_trace->Add("platform.window_create");

  SDL_RaiseWindow(window);

  SDL_PumpEvents();
  openwow::platform::WindowManager::Get().AdoptExternalWindow(window);

#if defined(_WIN32)
  SDL_SysWMinfo window_info;
  SDL_VERSION(&window_info.version);
  if (SDL_GetWindowWMInfo(window, &window_info) == SDL_TRUE &&
      window_info.subsystem == SDL_SYSWM_WINDOWS) {
    openwow::core::EvtSched_AttachWindowTimerHook(window_info.info.win.window);
  }
#endif

  if (startup_trace.has_value()) startup_trace->Add("launch.resolve");

  openwow::diagnostics::InitLogging("openwow-client", openwow::diagnostics::LogLevel::kInfo);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "Client startup");

  openwow::net::PacketLog::Get().Initialize(
      (user_data_root / "logs").string());
  std::cerr << "OpenWoW log: " << openwow::diagnostics::CurrentLogFile().string() << '\n';
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Content root: " + content_root.string());
  if (!override_content_root.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "Override content root: " + override_content_root.string());
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "User data root: " + user_data_root.string());

  {
    namespace fs = std::filesystem;
    const auto data_dir = content_root / "Data";
    if (!fs::is_directory(data_dir)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
          "Content root has no 'Data' directory — MPQ archives will NOT be "
          "loaded. Run from the WoW installation root, or use "
          "--game-data <path> / OPENWOW_GAME_DATA env var.");
      std::cerr << "WARNING: content root has no 'Data' subdirectory. "
                   "GlueXML/models/textures will not load. "
                   "Fix: run from the WoW install root or use "
                   "--game-data <wow_install_path>.\n";
    }
  }

  if (startup_trace.has_value()) startup_trace->Add("logging.init");

  openwow::core::InitializeClientStartupAdlerSeedState();

  int exit_code = 2;
  if (startup_trace.has_value()) startup_trace->Add("client.glue_construct");
  {
    openwow::client::GlueClient client(openwow::client::GlueClient::Options{
        .window = window,
        .launch_context = openwow::client::ClientLaunchContext{
            .content_root = content_root,
            .override_content_root = override_content_root,
            .diagnostic_output_root = artifacts_dir,
        },
        .scenario_opts = std::move(scenario_opts),
        .startup_trace = startup_trace.has_value() ? &*startup_trace : nullptr,
        .lua_trace = lua_trace.has_value() ? &*lua_trace : nullptr,
        .ui_frame_tree_dump_path = std::move(ui_frame_tree_dump_path),
        .render_submit_trace = render_submit_trace_ref,
        .render_submit_trace_path = std::move(render_submit_trace_path),
    });

    const bool profile_ready =
        !scenario_profile.has_value() ||
        scenario_profile->EnterClientWorkingDirectory();
    if (!profile_ready) {

      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                                "Failed to enter isolated scenario profile");
      exit_code = 1;
    } else {
      if (startup_trace.has_value())
        startup_trace->Add("client.glue_initialize.begin");
      if (client.Initialize()) {
        if (startup_trace.has_value())
          startup_trace->Add("client.glue_initialize.end");
        if (startup_trace.has_value()) startup_trace->Add("client.main_loop.begin");
        exit_code = client.Run();
      } else if (startup_trace.has_value()) {
        startup_trace->Add("client.glue_initialize.fail");
      }
    }

    if (startup_trace.has_value()) startup_trace->Add("client.shutdown.begin");
    client.Shutdown();
  }

  if (scenario_profile.has_value()) {
    scenario_profile->LeaveClientWorkingDirectory();
  }
  if (startup_trace.has_value()) startup_trace->Add("client.shutdown.end");
  write_startup_trace();
  if (lua_trace.has_value()) {
    (void)lua_trace->WriteTsvFile(artifacts_dir / "lua_trace" / "glue_lua_events.tsv");
  }

  openwow::platform::WindowManager::Get().Shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
  openwow::diagnostics::ShutdownLogging();
  return exit_code;
}

}

int main(int argc, char** argv) {

  return openwow::core::RunLegacyStartupFiberBootstrap(
      [&]() { return RunClientProcess(argc, argv); });
}
