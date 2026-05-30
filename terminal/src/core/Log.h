// Log.h — dead-simple stderr logging used across the terminal.
#pragma once
#include <cstdio>

#define LOGI(...) do { std::fprintf(stderr, "[INFO] "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define LOGW(...) do { std::fprintf(stderr, "[WARN] "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define LOGE(...) do { std::fprintf(stderr, "[ERR ] "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
