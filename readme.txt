Productivity Tracker - README (simple)
--------------------------------------

What this app does
- Tracks hourly quick logs, daily & weekly status entries, breaks, and simple hierarchical tasks.
- Persists data under: ~/.productivity_tracker/
  - daily_logs.txt       -- human-readable log lines
  - tasks.txt            -- task list
  - daily_status.txt     -- latest saved daily status
  - weekly_status.txt    -- latest saved weekly status
  - exported files       -- timestamped exports (daily_status_export_*.txt, weekly_logs_export_*.txt, etc.)
  - cleared_marker.txt   -- created when "Clear All" is used

How startup/load works (brief)
- On startup the app calls:
  - loadTasks()    -> reads ~/.productivity_tracker/tasks.txt and rebuilds the in-memory task list
  - loadDailyLogs() -> reads ~/.productivity_tracker/daily_logs.txt and rebuilds the in-memory logs list
- These functions only read the application's data directory (user home + .productivity_tracker), parse each line, and populate the in-memory vectors so the UI shows persisted state immediately.
- Exports are written as timestamped files in the same directory; they do not automatically change app state (they are standalone snapshots).

"Clear All" behavior
- The "Clear All" toolbar button opens a confirmation dialog.
- If confirmed:
  - the app clears the in-memory logs, breaks, and tasks.
  - deletes core files: daily_logs.txt, tasks.txt, daily_status.txt, weekly_status.txt in ~/.productivity_tracker.
  - writes a small cleared_marker.txt with a timestamp so you can see when clear occurred.
- This action is irreversible (it deletes persisted files).

Build / run (example)
- Requirements: C++17, GLFW, glad, ImGui and ImGui backends (imgui_impl_glfw, imgui_impl_opengl3)
- On macOS / Linux use CMake 
- On Windows, build via your preferred toolchain (MSVC / CMake), link glfw, opengl32, etc.
cmake ..
make -j32 # -j$(sysctl -n hw.cpu) # on macos
./productivity_tracker
