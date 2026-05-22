#include "strategy_runner.h"
#include "coinrunner_log.h"

#include "base/base_async_log.h"

#include <cstring>
#include <iostream>
#include <string>

using namespace nova::trade;

namespace {

void PrintUsage(const char *prog) {
  std::cout << "Usage: " << prog << " [options] [config.json]\n"
            << "Options:\n"
            << "  -f, --config <path>  Config JSON file\n"
            << "  -d, --data  <dir>    Backtest data directory or .bin file\n"
            << "  -h, --help           Show this help\n"
            << "\nMode is auto-detected from config JSON:\n"
            << "  Trade.TradeEngine with real engines → prod\n"
            << "  MockEngine + Quote.backtest       → backtest\n"
            << "  MockEngine + Quote.<exch>.enabled → mock\n"
            << std::endl;
}

} // namespace

int main(int argc, char **argv) {
  StrategyRunner runner;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg == "-f" || arg == "--config") {
      if (i + 1 < argc) runner.SetConfigFile(argv[++i]);
    } else if (arg == "-d" || arg == "--data") {
      if (i + 1 < argc) runner.SetDataDir(argv[++i]);
    } else if (arg[0] != '-') {
      runner.SetConfigFile(arg);
    }
  }

  if (!runner.Initialize()) {
    ERROR_FLOG("[Main] Init failed");
    return 1;
  }
  runner.Run();
  INFO_FLOG("[Main] Shutdown complete");
  return 0;
}
