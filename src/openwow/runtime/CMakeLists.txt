add_subdirectory(bootstrap)

target_sources(openwow_engine_core PRIVATE
  scheduling/task_scheduler.cpp
  scheduling/frame_scheduler.cpp
  scheduling/thread_pool_system.cpp
  scheduling/frame_job_system.cpp
  scheduling/evt_sched.cpp
  time/game_clock.cpp
  time/game_time.cpp
  time/frame_timing.cpp
)
