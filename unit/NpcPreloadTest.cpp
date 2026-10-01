#include "TestUtils.hpp"
#include "libespm/ACHR.h"
#include "libespm/REFR.h"
#include <algorithm>
#include <limits>

espm::Loader& GetEspmLoader();

namespace {
constexpr uint32_t kCowId = 0x10ebaf;

const std::vector<uint32_t>& PlacedActorIds()
{
  static const auto ids = [] {
    std::vector<uint32_t> result;
    const auto& browser = GetEspmLoader().GetBrowser();
    for (const auto& lookup : browser.GetDistinctRecordsByType("REFR")) {
      if (lookup.rec->GetType() == "ACHR") {
        result.push_back(lookup.ToGlobalId(lookup.rec->GetId()));
      }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  }();
  return ids;
}

size_t CursorFor(uint32_t actorId)
{
  const auto& ids = PlacedActorIds();
  const auto it = std::lower_bound(ids.begin(), ids.end(), actorId);
  REQUIRE(it != ids.end());
  REQUIRE(*it == actorId);
  return static_cast<size_t>(it - ids.begin());
}

void AttachFixture(WorldState& world)
{
  world.AttachEspm(&GetEspmLoader(), [] { return FormCallbacks::DoNothing(); });
  world.npcEnabled = true;
  world.npcAllowEssential = true;
  world.npcAllowCrimeFaction = true;
}

size_t KnownFormCount(WorldState& world)
{
  size_t count = 0;
  for (size_t i = 0; i < world.espmFiles.size(); ++i) {
    const auto forms = world.GetAllForms(static_cast<uint32_t>(i));
    REQUIRE(forms);
    count += forms->size();
  }
  return count;
}
}

TEST_CASE("An NPC batch loads an unseen placed actor without any human host",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(kCowId));
  const auto cursor = CursorFor(kCowId);

  const auto batch = world.LoadNpcBatch(cursor, 1);

  REQUIRE(batch.total == PlacedActorIds().size());
  REQUIRE(batch.nextCursor == cursor + 1);
  REQUIRE(batch.actorIds == std::vector<uint32_t>{ kCowId });
  const auto form = world.LookupFormByIdNoLoad(kCowId);
  REQUIRE(form);
  const auto actor = form->AsActor();
  REQUIRE(actor);
  REQUIRE(actor->GetUserId() == Networking::InvalidUserId);
  REQUIRE(actor->GetProfileId() == -1);
  REQUIRE(world.hosters.empty());
  REQUIRE(world.lastMovUpdateByIdx.empty());

  const auto record = GetEspmLoader().GetBrowser().LookupById(kCowId);
  const auto data = reinterpret_cast<const espm::REFR*>(record.rec)
                      ->GetData(world.GetEspmCache());
  REQUIRE(data.loc);
  REQUIRE(actor->GetBaseId() == record.ToGlobalId(data.baseId));
  REQUIRE(actor->GetPos() ==
          NiPoint3{ data.loc->pos[0], data.loc->pos[1], data.loc->pos[2] });
  // Return-count bounds alone would miss recursive 3x3 chunk loading.
  REQUIRE(KnownFormCount(world) == 1);
}

TEST_CASE("NPC batch arguments and end cursor are bounded",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  world.npcEnabled = false;
  const auto total = PlacedActorIds().size();
  REQUIRE(total > 0);

  REQUIRE_THROWS(world.LoadNpcBatch(0, 0));
  REQUIRE_THROWS(world.LoadNpcBatch(0, 129));
  REQUIRE_THROWS(world.LoadNpcBatch(total + 1, 1));
  REQUIRE_THROWS(world.LoadNpcBatch(std::numeric_limits<size_t>::max(), 1));
  const auto end = world.LoadNpcBatch(total, 128);
  REQUIRE(end.total == total);
  REQUIRE(end.nextCursor == total);
  REQUIRE(end.actorIds.empty());
  REQUIRE(KnownFormCount(world) == 0);
}

TEST_CASE("Normal chunk streaming resumes after a bounded NPC load",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  world.LoadNpcBatch(CursorFor(kCowId), 1);
  REQUIRE(KnownFormCount(world) == 1);
  const auto actor = world.LookupFormByIdNoLoad(kCowId)->AsActor();
  const auto pos = actor->GetPos();
  const auto cell = actor->GetCellOrWorld().ToFormId(world.espmFiles);
  const auto& neighbours = world.GetNeighborsByPosition(
    cell, static_cast<int16_t>(pos.x / 4096),
    static_cast<int16_t>(pos.y / 4096));
  REQUIRE(neighbours.size() > 1);
}

TEST_CASE("A failed NPC load restores normal chunk streaming",
          "[NpcPreload][WorldState][espm]")
{
  bool throwOnSubscribe = true;
  WorldState world;
  AttachFixture(world);
  world.AttachEspm(&GetEspmLoader(), [&] {
    auto callbacks = FormCallbacks::DoNothing();
    callbacks.subscribe = [&](auto, auto) {
      if (throwOnSubscribe) {
        throw std::runtime_error("preload subscription failure");
      }
    };
    return callbacks;
  });
  REQUIRE_THROWS_WITH(world.LoadNpcBatch(CursorFor(kCowId), 1),
                      "preload subscription failure");
  throwOnSubscribe = false;
  const auto form = world.LookupFormByIdNoLoad(kCowId);
  REQUIRE(form);
  const auto actor = form->AsActor();
  REQUIRE(actor);
  const auto pos = actor->GetPos();
  const auto cell = actor->GetCellOrWorld().ToFormId(world.espmFiles);
  const auto& neighbours = world.GetNeighborsByPosition(
    cell, static_cast<int16_t>(pos.x / 4096),
    static_cast<int16_t>(pos.y / 4096));
  REQUIRE(neighbours.size() > 1);
}

TEST_CASE("Streaming a preloaded NPC does not allocate abandoned form indexes",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  world.LoadNpcBatch(CursorFor(kCowId), 1);
  const auto original = world.LookupFormByIdNoLoad(kCowId);
  const auto actor = original->AsActor();
  const auto pos = actor->GetPos();
  world.GetNeighborsByPosition(
    actor->GetCellOrWorld().ToFormId(world.espmFiles),
    static_cast<int16_t>(pos.x / 4096),
    static_cast<int16_t>(pos.y / 4096));
  REQUIRE(world.LookupFormByIdNoLoad(kCowId).get() == original.get());

  std::vector<uint32_t> liveIndexes;
  for (size_t i = 0; i < world.espmFiles.size(); ++i) {
    for (const auto id : *world.GetAllForms(static_cast<uint32_t>(i))) {
      const auto form = world.LookupFormByIdNoLoad(id);
      REQUIRE(form);
      const auto ref = form->AsObjectReference();
      REQUIRE(ref);
      liveIndexes.push_back(ref->GetIdx());
    }
  }
  std::sort(liveIndexes.begin(), liveIndexes.end());
  std::vector<uint32_t> expected(liveIndexes.size());
  for (uint32_t i = 0; i < expected.size(); ++i) {
    expected[i] = i;
  }
  // No references were destroyed. Every allocated index must still belong
  // to one live form, rather than a discarded duplicate attachment.
  REQUIRE(liveIndexes == expected);
}

TEST_CASE("NPC pagination progresses through policy refusals and terminates",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  world.npcEnabled = false;
  const auto total = PlacedActorIds().size();
  REQUIRE(total > 0);
  size_t cursor = 0;
  size_t batches = 0;
  while (cursor < total) {
    const auto batch = world.LoadNpcBatch(cursor, 128);
    REQUIRE(batch.total == total);
    REQUIRE(batch.nextCursor == std::min(cursor + 128, total));
    REQUIRE(batch.actorIds.empty());
    cursor = batch.nextCursor;
    ++batches;
  }
  REQUIRE(batches == (total + 127) / 128);
  const auto end = world.LoadNpcBatch(cursor, 1);
  REQUIRE(end.nextCursor == cursor);
  REQUIRE(end.actorIds.empty());
  REQUIRE(KnownFormCount(world) == 0);
}

TEST_CASE("Repeating an NPC batch keeps its identity and current transform",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  const auto cursor = CursorFor(kCowId);
  const auto first = world.LoadNpcBatch(cursor, 1);
  REQUIRE(first.actorIds == std::vector<uint32_t>{ kCowId });
  const auto original = world.LookupFormByIdNoLoad(kCowId);
  REQUIRE(original);
  const auto actor = original->AsActor();
  REQUIRE(actor);
  const auto idx = actor->GetIdx();
  const NiPoint3 moved{ 123, 456, 789 };
  actor->SetPos(moved);

  const auto repeated = world.LoadNpcBatch(cursor, 1);

  REQUIRE(repeated.actorIds == first.actorIds);
  REQUIRE(repeated.nextCursor == first.nextCursor);
  REQUIRE(world.LookupFormByIdNoLoad(kCowId).get() == original.get());
  REQUIRE(actor->GetIdx() == idx);
  REQUIRE(actor->GetPos() == moved);
  REQUIRE(world.hosters.empty());
}

TEST_CASE("An NPC batch restores a saved unseen placement before returning it",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  const auto lookup = GetEspmLoader().GetBrowser().LookupById(kCowId);
  REQUIRE(lookup.rec);
  const auto data = reinterpret_cast<const espm::REFR*>(lookup.rec)
                      ->GetData(world.GetEspmCache());
  MpChangeForm saved;
  saved.recType = MpChangeForm::ACHR;
  saved.formDesc = FormDesc::FromFormId(kCowId, world.espmFiles);
  saved.baseDesc =
    FormDesc::FromFormId(lookup.ToGlobalId(data.baseId), world.espmFiles);
  saved.position = { 123, 456, 789 };
  saved.angle = { 0, 0, 33 };
  saved.worldOrCellDesc = FormDesc::Tamriel();
  world.LoadChangeForm(saved, FormCallbacks::DoNothing());
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(kCowId));

  const auto batch = world.LoadNpcBatch(CursorFor(kCowId), 1);

  REQUIRE(batch.actorIds == std::vector<uint32_t>{ kCowId });
  const auto original = world.LookupFormByIdNoLoad(kCowId);
  REQUIRE(original);
  const auto actor = original->AsActor();
  REQUIRE(actor);
  REQUIRE(actor->GetPos() == saved.position);
  REQUIRE(actor->GetAngle() == saved.angle);
  REQUIRE(actor->GetCellOrWorld() == saved.worldOrCellDesc);
  REQUIRE(actor->GetUserId() == Networking::InvalidUserId);
  REQUIRE(KnownFormCount(world) == 1);
  world.LoadNpcBatch(CursorFor(kCowId), 1);
  REQUIRE(world.LookupFormByIdNoLoad(kCowId).get() == original.get());
  REQUIRE(actor->GetPos() == saved.position);
}

TEST_CASE("NPC batches do not bypass existing global and source policies",
          "[NpcPreload][WorldState][espm]")
{
  WorldState world;
  AttachFixture(world);
  SECTION("NPCs disabled") { world.npcEnabled = false; }
  SECTION("The reference's source plugin is disallowed")
  {
    world.SetNpcSettings({ { "Missing-Npc-Source.esp", { true, true } } });
  }
  SECTION("Only interior placements are allowed")
  {
    world.SetNpcSettings({ { "Skyrim.esm", { true, false } } });
  }
  const auto cursor = CursorFor(kCowId);
  const auto batch = world.LoadNpcBatch(cursor, 1);
  REQUIRE(batch.nextCursor == cursor + 1);
  REQUIRE(batch.actorIds.empty());
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(kCowId));
  REQUIRE(KnownFormCount(world) == 0);
}

TEST_CASE("NPC batches retain initially disabled and starts-dead reference gates",
          "[NpcPreload][WorldState][espm]")
{
  const auto& browser = GetEspmLoader().GetBrowser();
  WorldState world;
  AttachFixture(world);
  bool startsDead = false;
  SECTION("Initially disabled reference") {}
  SECTION("Starts-dead reference") { startsDead = true; }
  const auto& ids = PlacedActorIds();
  const auto it = std::find_if(ids.begin(), ids.end(), [&](uint32_t id) {
    const auto lookup = browser.LookupById(id);
    const auto achr = reinterpret_cast<const espm::ACHR*>(lookup.rec);
    return startsDead ? achr->StartsDead()
                      : (lookup.rec->GetFlags() & 0x800) != 0;
  });
  REQUIRE(it != ids.end());
  const auto batch = world.LoadNpcBatch(CursorFor(*it), 1);
  REQUIRE(batch.actorIds.empty());
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(*it));
  REQUIRE(KnownFormCount(world) == 0);
}

TEST_CASE("Attaching another ESPM loader invalidates the NPC candidate index",
          "[NpcPreload][WorldState][espm]")
{
  espm::Loader emptyLoader(std::vector<std::filesystem::path>{});
  WorldState world;
  AttachFixture(world);
  world.npcEnabled = false;
  const auto before = world.LoadNpcBatch(0, 1);
  REQUIRE(before.total == PlacedActorIds().size());
  REQUIRE(before.total > 0);
  world.AttachEspm(&emptyLoader, [] { return FormCallbacks::DoNothing(); });

  const auto after = world.LoadNpcBatch(0, 1);

  REQUIRE(after.total == 0);
  REQUIRE(after.nextCursor == 0);
  REQUIRE(after.actorIds.empty());
}
