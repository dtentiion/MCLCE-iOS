# Probe target: try to compile upstream Minecraft.World sources for iOS.
#
# Flip on with -DENABLE_WORLD_PROBE=ON. Expected to fail loudly. Each failure
# here is a signal about another Windows-ism or missing platform shim that
# has to be addressed as part of Phase 1.
#
# Start with a tiny set (just AABB for now) and grow as things compile.
# Do not link this into the app yet; it exists to drive a TU-compile pass
# only.

set(WORLD_PROBE_SOURCES
  ${UPSTREAM_DIR}/Minecraft.World/AABB.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Vec3.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Class.cpp
  ${UPSTREAM_DIR}/Minecraft.World/StringHelpers.cpp
  ${UPSTREAM_DIR}/Minecraft.World/PerformanceTimer.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Random.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Mth.cpp
  ${UPSTREAM_DIR}/Minecraft.World/TilePos.cpp
  # NBT + IO layer. Most Tag derivatives are header-only; the
  # .cpp files below cover Tag base + the IO read/write layer.
  ${UPSTREAM_DIR}/Minecraft.World/Tag.cpp
  ${UPSTREAM_DIR}/Minecraft.World/NbtIo.cpp
  ${UPSTREAM_DIR}/Minecraft.World/DataInputStream.cpp
  ${UPSTREAM_DIR}/Minecraft.World/DataOutputStream.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ByteArrayInputStream.cpp
  # Shallow files with no Entity / ItemInstance dependencies. Each
  # is under 600 lines with at most 4 includes.
  # AirTile.cpp dropped: AirTile.h pulls Tile.h which references
  # ItemInstance, LivingEntity, Explosion. The eMinecraftColour
  # piece is now resolvable via App_enums.h pre-include but the
  # other refs need the Tile / Item / Entity subsystems.
  # AnimalChest.cpp dropped: needs ItemInstanceArray (patched out
  # at ArrayWithLength.h). The IDS_ side is unblocked but Item
  # subsystem is still gated.
  # Re-attempt Attribute.cpp + BaseAttribute.cpp now that
  # eATTRIBUTE_ID is pre-included via Attribute.h. They still
  # need IDS_ATTRIBUTE_NAME_* loc constants - if those are
  # missing this round will fail again on those.
  ${UPSTREAM_DIR}/Minecraft.World/Attribute.cpp
  ${UPSTREAM_DIR}/Minecraft.World/BaseAttribute.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Abilities.cpp
  # Re-attempts after enum + IDS pre-include unlocks. Each of these
  # was dropped earlier on a missing enum or IDS_ constant. Try
  # again now; if any still fails it is on a different blocker
  # (entity / item / sync subsystem) and will be dropped again.
  # AttributeModifier.cpp dropped: needs global `app` (CMinecraftApp
  # instance) for app.GetString(). Re-add when the global app stub
  # lands.
  # Add*Layer biome generators dropped (again): they include
  # net.minecraft.world.level.biome.h -> Biome.h -> Mob.h ->
  # LivingEntity.h -> Entity.h, hitting the entity subsystem wall.
  # Re-add when ItemInstance + entity hierarchy land.
  ${UPSTREAM_DIR}/Minecraft.World/ArrowDamageEnchantment.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ArrowFireEnchantment.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ArrowInfiniteEnchantment.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ArrowKnockbackEnchantment.cpp
  # Next batch: more shallow files that do not transitively pull
  # Entity / Tile / Mob / Item / Biome.
  # BaseAttributeMap.cpp dropped: accesses ItemInstance members via
  # shared_ptr - needs full ItemInstance type, not forward decl.
  ${UPSTREAM_DIR}/Minecraft.World/BinaryHeap.cpp
  # BoundingBox.cpp dropped: needs full IntArrayTag type for `new
  # IntArrayTag(...)`. Forward decl is not enough. Re-add when the
  # full Tag hierarchy compiles.
  ${UPSTREAM_DIR}/Minecraft.World/Buffer.cpp
  ${UPSTREAM_DIR}/Minecraft.World/BufferedOutputStream.cpp
  ${UPSTREAM_DIR}/Minecraft.World/BufferedReader.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ByteArrayOutputStream.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ByteBuffer.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Calendar.cpp
  # World-gen feature files dropped: they all pull Tile.h
  # transitively which references Entity hierarchy with non-pointer
  # ItemInstance fields. Re-add when ItemInstance is ported.
  # ItemInstance.cpp dropped: pulls Stat / GenericStats which
  # references Player / Mob / DamageSource / eAward / LocalPlayer.
  # Real port would need the whole stat + player + entity chain
  # brought up together.
  ${UPSTREAM_DIR}/Minecraft.World/AwardStatPacket.cpp
  # AddExperienceOrbPacket.cpp dropped: includes
  # net.minecraft.world.entity.h (Entity hierarchy).
  ${UPSTREAM_DIR}/Minecraft.World/ChunkVisibilityPacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/DataLayer.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Direction.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Facing.cpp
  ${UPSTREAM_DIR}/Minecraft.World/FacingEnum.cpp
  ${UPSTREAM_DIR}/Minecraft.World/DoorInfo.cpp
  ${UPSTREAM_DIR}/Minecraft.World/DummyCriteria.cpp
  # EnchantmentCategory.cpp dropped: pulls every item header.
  ${UPSTREAM_DIR}/Minecraft.World/Distort.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Emboss.cpp
  ${UPSTREAM_DIR}/Minecraft.World/FastNoise.cpp
  # File.cpp dropped: heavy Win32 file system (WIN32_FIND_DATA,
  # MoveFile, GetLastError, MAX_PATH, wstringtofilename helper).
  ${UPSTREAM_DIR}/Minecraft.World/FileHeader.cpp
  ${UPSTREAM_DIR}/Minecraft.World/FloatBuffer.cpp
  # FoliageColor.cpp dropped: pulls Textures.h (C4JRender) + needs ColourTable.
  ${UPSTREAM_DIR}/Minecraft.World/FoodConstants.cpp
  ${UPSTREAM_DIR}/Minecraft.World/FuzzyZoomLayer.cpp
  # GameRules.cpp dropped: heavy use of global `app`.
  # GameCommandPacket.cpp dropped: uses `app`.
  ${UPSTREAM_DIR}/Minecraft.World/GameEventPacket.cpp
  # GeneralStat.cpp dropped: pulls GenericStats with eAward + app.
  ${UPSTREAM_DIR}/Minecraft.World/GetInfoPacket.cpp
  # FlatLayerInfo + FlatGeneratorInfo dropped, pull Biome/Tile.
  ${UPSTREAM_DIR}/Minecraft.World/FlippedIcon.cpp
  # Hasher.cpp dropped: needs MSVC's <xhash> header.
  # HtmlString.cpp dropped: needs escapeXML helper + global app.
  ${UPSTREAM_DIR}/Minecraft.World/I18n.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ImprovedNoise.cpp
  ${UPSTREAM_DIR}/Minecraft.World/InputStream.cpp
  ${UPSTREAM_DIR}/Minecraft.World/InputStreamReader.cpp
  ${UPSTREAM_DIR}/Minecraft.World/IntBuffer.cpp
  ${UPSTREAM_DIR}/Minecraft.World/IntCache.cpp
  ${UPSTREAM_DIR}/Minecraft.World/JavaMath.cpp
  ${UPSTREAM_DIR}/Minecraft.World/KeepAlivePacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/KickPlayerPacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Language.cpp
  # PacketListener dropped: pulls every packet header.
  ${UPSTREAM_DIR}/Minecraft.World/PerlinNoise.cpp
  ${UPSTREAM_DIR}/Minecraft.World/PerlinSimplexNoise.cpp
  ${UPSTREAM_DIR}/Minecraft.World/PlayerInputPacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Pos.cpp
  # PreLoginPacket dropped: SHORT type + MINECRAFT_NET constants + app.
  ${UPSTREAM_DIR}/Minecraft.World/RangedAttribute.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Rarity.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Rotate.cpp
  ${UPSTREAM_DIR}/Minecraft.World/RotateHeadPacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Scale.cpp
  ${UPSTREAM_DIR}/Minecraft.World/SetCarriedItemPacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/SetDisplayObjectivePacket.cpp
  ${UPSTREAM_DIR}/Minecraft.World/Team.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ThreadName.cpp
  # TickNextTickData dropped: pulls Tile chain.
  ${UPSTREAM_DIR}/Minecraft.World/TileEventData.cpp
  ${UPSTREAM_DIR}/Minecraft.World/VoronoiZoom.cpp
  ${UPSTREAM_DIR}/Minecraft.World/WaterColor.cpp
  # Biome layer files all dropped - all pull Biome -> Mob -> Entity.
  # Pivot: try a Minecraft.Client utility cpp that does not pull
  # the entity subsystem. ColourTable maps colour names to
  # eMinecraftColour values, pure data + StringHelpers.
  ${UPSTREAM_DIR}/Minecraft.Client/Common/Colours/ColourTable.cpp
  # Tile-derived files (small ones) - retry now Tile.h parses.
  ${UPSTREAM_DIR}/Minecraft.World/SandStoneTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/RotatedPillarTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/TransparentTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/TorchTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/ThinFenceTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/HellStoneTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/HugeMushroomTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/HayBlockTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/PoweredMetalTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/CarrotTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/BookshelfTile.cpp
  ${UPSTREAM_DIR}/Minecraft.World/LevelConflictException.cpp
  # LevelSettings.cpp dropped: pulls level.storage.h which pulls entity.
  # BehaviorRegistry.cpp also pulls Entity transitively, dropping.
  # BlockReplacements.cpp dropped: includes ChestTile, HopperTile,
  # StairTile, TntTile, etc which all reference Explosion / Random
  # fields and Container types.
  # AttackDamageMobEffect retry: ItemInstance now a stub class so
  # the entity field-layout parse should succeed.
  ${UPSTREAM_DIR}/Minecraft.World/AttackDamageMobEffect.cpp
  # Big retry batch with ItemInstance stub.
  ${UPSTREAM_DIR}/Minecraft.World/ChunkPos.cpp
  ${UPSTREAM_DIR}/Minecraft.World/AirTile.cpp
  # All biome layer + override layer files dropped - they pull
  # net.minecraft.world.level.biome.h which transitively hits
  # PortalForcer.h's incomplete base class.
  ${UPSTREAM_DIR}/Minecraft.World/AnimalChest.cpp
  # BasicTree dropped - PIX profiler + app.
  # BirchFeature dropped - app + LevelGenerationOptions.
  # CactusFeature dropped - PortalForcer base class incomplete.
  # CaveFeature dropped - uses global `app` + LevelGenerationOptions.
  # CanyonFeature dropped - uses app.
  # Bush.cpp dropped - PortalForcer chain.
  # BaseAttributeMap dropped - calls real ItemInstance methods.
  # Add*Layer biome generators: pull BiomeSource and friends, also
  # heavy. Re-add with the world-gen subsystem port.
  # Arrow enchantments dropped: Enchantment.h needs LivingEntity,
  # HtmlString, ItemInstance, plus each cpp needs IDS_ENCHANTMENT_*
  # localization constants. Re-add when entity / item / loc layers
  # are ported.
  # AttackDamageMobEffect.cpp dropped: pulls AttributeModifier.h,
  # MobEffect.h, eMinecraftColour, LivingEntity, BaseAttributeMap.
  ${UPSTREAM_DIR}/Minecraft.World/BasicTypeContainers.cpp
  # compression.cpp deferred. It needs ByteOrder + LITTLEENDIAN
  # from Definitions.h (stdafx is no-op so we have to surface
  # those some other way), plus Win32 CriticalSection shims, plus
  # Xbox PIX profiling no-ops, plus an XMEMCOMPRESSION_CONTEXT stub
  # for the Xbox compression branch we will not enter. Punt until
  # those shims land for other reasons.
  # ChunkPos.cpp dropped: it does
  #   #include "net.minecraft.world.entity.h"
  # which pulls Entity / Creature / SynchedEntityData / EntityIO and a
  # cascade of enums (eINSTANCEOF, eMinecraftColour, eTYPE_DERIVED_FROM)
  # plus ItemInstance. Add back when the entity subsystem is brought
  # into the probe deliberately.
)

add_library(mcle_world_probe STATIC
  ${WORLD_PROBE_SOURCES}
  probe_stub.cpp
)

target_include_directories(mcle_world_probe PRIVATE
  ${UPSTREAM_DIR}/Minecraft.World
  ${UPSTREAM_DIR}/Minecraft.World/x64headers
  ${UPSTREAM_DIR}/include
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/compat_headers
)

# Force-include the iOS PCH-equivalent at the top of every TU. iOS_stdafx.h
# pulls in the C++ standard headers upstream's stdafx.h chain assumes plus
# iOS_WinCompat.h's Win32 type aliases, so upstream files that start with
# `#include "stdafx.h"` resolve cleanly without us having to edit them.
target_compile_options(mcle_world_probe PRIVATE
  -include "${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/iOS_stdafx.h"
  # Upstream uses __declspec(thread) for thread-local storage in a few
  # files (Tag.cpp etc). clang understands the syntax under -fdeclspec
  # and maps it to thread_local for static storage.
  -fdeclspec
)

target_compile_definitions(mcle_world_probe PRIVATE
  _LIB
  _LARGE_WORLDS
  # Skip debug/profiling code paths that need the global `app`
  # (CMinecraftApp instance). PerformanceTimer.cpp gates its body on
  # !_CONTENT_PACKAGE; many other Minecraft.World files do the same.
  # We do not have a real `app` to bind against in the probe so we
  # take the content-only branch.
  _CONTENT_PACKAGE
)

# Pin to C++14. Upstream is 2014-era code that doesn't need anything newer,
# and C++17's std::byte clashes with extraX64.h:14's `typedef unsigned char
# byte;` whenever upstream files do `using namespace std;` (which is most of
# them). The main app target stays on C++17; this only affects the probe
# static lib so there's no ABI-mixing concern at link time.
set_target_properties(mcle_world_probe PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
