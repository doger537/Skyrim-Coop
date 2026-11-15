#include <EncodingPch.h>
#include <catch2/catch.hpp>
#include <Messages/NotifyActorTeleport.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/ActorTeleportAuthority.h>
#include <TiltedCore/Buffer.hpp>
#include <limits>

TEST_CASE("Actor teleport carries ownership provenance through the network factory", "[encoding.teleport]")
{
    NotifyActorTeleport sent;
    sent.ServerId = 0xE00024;
    sent.OwnershipEpoch = std::numeric_limits<uint32_t>::max();
    sent.FormId = GameId{0, 0x105A35};
    sent.WorldSpaceId = GameId{0, 0x3C};
    sent.CellId = GameId{0, 0x9771};
    sent.Position = {14720.f, -53248.f, 970.f};

    TiltedPhoques::Buffer buffer(512);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    sent.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto decoded = factory.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == NotifyActorTeleport::Opcode);
    auto received = TiltedPhoques::CastUnique<NotifyActorTeleport>(std::move(decoded));
    REQUIRE(*received == sent);

    received->OwnershipEpoch = 1;
    REQUIRE_FALSE(*received == sent);
    received->OwnershipEpoch = sent.OwnershipEpoch;
    received->ServerId++;
    REQUIRE_FALSE(*received == sent);
}

TEST_CASE("Owner teleport cannot overwrite a newer or locally authoritative incarnation", "[ownership.teleport]")
{
    NotifyActorTeleport release;
    release.ServerId = 0xE00024;
    release.OwnershipEpoch = 7;
    release.FormId = GameId{0, 0x105A35};
    ActorTeleportAuthority::Target remote{release.ServerId, 7, release.FormId, false};

    REQUIRE(ActorTeleportAuthority::CanApply(release, remote));
    remote.OwnershipEpoch = 8; // The next owner's transfer arrived first.
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(release, remote));

    remote.IsLocalOwner = true;
    release.OwnershipEpoch = 8;
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(release, remote));
    release.OwnershipEpoch = 9;
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(release, remote));

    remote.IsLocalOwner = false;
    REQUIRE(ActorTeleportAuthority::CanApply(release, remote)); // Missing transfer, like other owner deltas.
}

TEST_CASE("Owner teleport requires the same tracked network actor and form", "[ownership.teleport]")
{
    NotifyActorTeleport teleport;
    teleport.ServerId = 0xE00024;
    teleport.OwnershipEpoch = 2;
    teleport.FormId = GameId{0, 0x105A35};
    const ActorTeleportAuthority::Target target{teleport.ServerId, 2, teleport.FormId, false};
    REQUIRE(ActorTeleportAuthority::CanApply(teleport, target));
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(teleport, std::nullopt));

    auto different = target;
    different.ServerId = 0xF00024; // ECS slot reused after destruction, same static form.
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(teleport, different));
    different = target;
    different.FormId.ModId = 3; // Same local base ID in another plugin.
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(teleport, different));

    teleport.OwnershipEpoch = 0;
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(teleport, target));
    teleport.OwnershipEpoch = 2;
    different = target;
    different.OwnershipEpoch = 0;
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(teleport, different));
    teleport.FormId = GameId{};
    REQUIRE_FALSE(ActorTeleportAuthority::CanApply(teleport, target));
}

TEST_CASE("Final placement survives a retired static actor without bypassing current authority", "[ownership.teleport]")
{
    NotifyActorTeleport teleport;
    teleport.ServerId = 0xE00024; // Server may destroy this incarnation after its last owner leaves.
    teleport.OwnershipEpoch = 2;
    teleport.FormId = GameId{0, 0x105A35};
    REQUIRE(ActorTeleportAuthority::CanApplyUntracked(teleport, false));
    // Same form under a new ServerId, or locally owned: never use the fallback.
    REQUIRE_FALSE(ActorTeleportAuthority::CanApplyUntracked(teleport, true));

    teleport.OwnershipEpoch = 0;
    REQUIRE_FALSE(ActorTeleportAuthority::CanApplyUntracked(teleport, false));
    teleport.OwnershipEpoch = 2;
    teleport.FormId = GameId{};
    REQUIRE_FALSE(ActorTeleportAuthority::CanApplyUntracked(teleport, false));
    teleport.FormId = GameId{0, 0x14};
    REQUIRE_FALSE(ActorTeleportAuthority::CanApplyUntracked(teleport, false));
    teleport.FormId = GameId{std::numeric_limits<uint32_t>::max(), 0xFF000001};
    REQUIRE_FALSE(ActorTeleportAuthority::CanApplyUntracked(teleport, false));
}
