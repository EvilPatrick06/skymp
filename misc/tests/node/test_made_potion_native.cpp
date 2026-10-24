// Thornswood #1649. The game-free half of createMadePotion (MadePotion.h):
// which requests it builds, and what each effect costs.
#include "MadePotion.h"
#include <cmath>
#include <cstdio>
#include <limits>

static int pass = 0, fail = 0;
static void Yes(const char* what, bool cond)
{
  std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
  (cond ? pass : fail)++;
}

static MadePotion::Request Good()
{
  MadePotion::Request r;
  r.formId = 0xff9a3c11;
  r.effectIds = { 0x3eb15, 0x3eaf3 };
  r.magnitudes = { 23.45, 18.2 };
  r.areas = { 0, 0 };
  r.durations = { 0, 60 };
  return r;
}

int main()
{
  Yes("a brew the server sends is well formed",
      MadePotion::IsWellFormed(Good()));

  auto r = Good();
  r.formId = 0xff000a01;
  Yes("an id the game hands out itself is not built over",
      !MadePotion::IsWellFormed(r));
  r = Good();
  r.formId = 0x3eadd;
  Yes("nor a plugin's id", !MadePotion::IsWellFormed(r));
  Yes("the made range starts where the server's does",
      MadePotion::IsMadeId(0xff800000) && !MadePotion::IsMadeId(0xff7fffff));

  r = Good();
  r.effectIds.clear();
  r.magnitudes.clear();
  r.areas.clear();
  r.durations.clear();
  Yes("a bottle with nothing in it is not built", !MadePotion::IsWellFormed(r));

  r = Good();
  r.durations.pop_back();
  Yes("lists of different lengths are not built", !MadePotion::IsWellFormed(r));

  r = Good();
  r.effectIds[1] = 0;
  Yes("an effect with no id is not built", !MadePotion::IsWellFormed(r));

  r = Good();
  r.magnitudes[0] = std::numeric_limits<double>::quiet_NaN();
  Yes("a NaN magnitude is not built", !MadePotion::IsWellFormed(r));
  r = Good();
  r.magnitudes[0] = -1;
  Yes("a negative magnitude is not built", !MadePotion::IsWellFormed(r));
  r = Good();
  r.durations[1] = 60.5;
  Yes("half a second of duration is not built", !MadePotion::IsWellFormed(r));

  r = Good();
  for (int i = 0; i < 7; ++i) {
    r.effectIds.push_back(0x3eb15);
    r.magnitudes.push_back(1);
    r.areas.push_back(0);
    r.durations.push_back(0);
  }
  Yes("more than kMaxEffects effects are not built",
      !MadePotion::IsWellFormed(r));

  auto near = [](double a, double b) { return std::fabs(a - b) < 1e-3; };
  Yes("cost: base x magnitude^1.1 for an instant effect",
      near(MadePotion::Cost(1.f, 10.f, 0), std::pow(10.0, 1.1)));
  Yes("cost: a magnitude under 1 counts as 1, a duration under 10 as 10",
      near(MadePotion::Cost(2.f, 0.5f, 5), 2.0));
  Yes("cost: duration counts by tens, to the 1.1",
      near(MadePotion::Cost(1.f, 1.f, 20), std::pow(2.0, 1.1)));

  std::printf("%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
