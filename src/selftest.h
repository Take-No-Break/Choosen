// Developer tools built into Choosen.exe. They run in a temporary folder and never touch data\.
//   --selftest   correctness tests (scan, journal, ratings, corrections, restart, rename)
//   --bench      timing of the hot APIs on a synthetic 150,000-image library
//   --simulate   long-run behaviour of the review scheduler on a synthetic library
// Results go to build\selftest.txt (and the console when started from one); exit code 0 = pass.
#pragma once
#include <string>

int runTool(const std::wstring& tool);
