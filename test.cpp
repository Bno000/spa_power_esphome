#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>
#include "../components/spa_power/spa_power_parser.h"
using namespace esphome::spa_power;
int main(int, char **argv) {
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  SpaFrameParser p; SpaDecoder d;
  std::map<int,int> types; int idx = 0;
  for (uint8_t b : data) {
    if (p.feed(b)) {
      idx++;
      types[p.frame().type]++;
      SpaUpdate u; d.process(p.frame(), u);
      if (u.has_current_temp) printf("  [%5d] current_temp %.2f\n", idx, u.current_temp);
      if (u.has_set_temp)     printf("  [%5d] set_temp     %.2f\n", idx, u.set_temp);
      if (u.has_light)        printf("  [%5d] light        %.0f%%\n", idx, u.light_percent);
      if (u.has_pump)         printf("  [%5d] pump         %s\n", idx, u.pump.c_str());
      if (u.has_key)          printf("  [%5d] key          %s\n", idx, u.key.c_str());
      if (u.has_line1)        printf("  [%5d] line1        '%s'\n", idx, u.line1.c_str());
      if (u.has_line2)        printf("  [%5d] line2        '%s'\n", idx, u.line2.c_str());
    }
  }
  printf("bytes %zu | frames ok %u bad %u resync %u overflow %u\n", data.size(), p.frames_ok(), p.frames_bad(), p.resyncs(), p.overflows());
  printf("types:"); for (auto &kv : types) printf(" %02x=%d", kv.first, kv.second); printf("\n");
}
