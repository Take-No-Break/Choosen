# Photo ranking engine and Windows host

Choosen's shared C++20 engine indexes photos, records ratings, schedules reviews, calculates rankings and exports selected images. Windows also uses the HTTP server, thumbnail decoder and desktop launcher here; Android builds the engine with its own platform adapters.

## Entry points

| File | Responsibility |
|---|---|
| [core.h](core.h) | Shared engine interface. |
| [core_library.cpp](core_library.cpp) | Photo indexing and library data. |
| [core_journal.cpp](core_journal.cpp) | Rating journal and event persistence. |
| [core_schedule.cpp](core_schedule.cpp) | Review scheduling. |
| [core_export.cpp](core_export.cpp) | Export operations. |
| [api_routes.cpp](api_routes.cpp) | Requests from the application interfaces. |
| [platform.h](platform.h) | Platform adapter contract. |
| [main.cpp](main.cpp) | Windows startup and application hosting. |

See the [architecture and build guide](../docs/ARCHITECTURE.md) for platform integration and build requirements.
