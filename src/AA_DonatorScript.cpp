#include "Chat.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
    constexpr auto kCacheLifetime = std::chrono::seconds(45);
    constexpr std::string_view kAddonName = "AADonatorIcons";
    constexpr std::string_view kWirePrefix = "AADONOR1|";

    // CoA's GetClientAddonNames() has been unreliable for this custom client.
    // Disabled by default so a donor's identity is never gated by that list.
    // IMPORTANT: clients without the addon can see raw AADONOR1 system records.
    // Enable this only after confirming addon advertisements work on your build.
    constexpr bool kRequireAdvertisedAddonForRecipient = false;

    struct Donor
    {
        uint8_t tier = 0;
        std::chrono::steady_clock::time_point checked{};
    };

    std::unordered_map<uint32_t, Donor> g_donors;

    bool HasAddon(WorldSession const* session)
    {
        if (!session)
            return false;

        for (std::string const& advertised : session->GetClientAddonNames())
        {
            // Addon metadata can differ in case on customized clients.
            if (advertised.size() != kAddonName.size())
                continue;

            bool same = true;
            for (size_t i = 0; i < advertised.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(advertised[i])) !=
                    std::tolower(static_cast<unsigned char>(kAddonName[i])))
                {
                    same = false;
                    break;
                }
            }
            if (same)
                return true;
        }
        return false;
    }

    uint8_t TierOf(Player* player, bool force = false)
    {
        if (!player || !player->GetSession())
            return 0;

        uint32_t const account = player->GetSession()->GetAccountId();
        auto const now = std::chrono::steady_clock::now();
        auto const it = g_donors.find(account);
        if (!force && it != g_donors.end() && (now - it->second.checked) < kCacheLifetime)
            return it->second.tier;

        uint8_t tier = 0;
        auto result = LoginDatabase.Query(
            "SELECT donator FROM aa_donators WHERE account_id = {} LIMIT 1", account);
        if (result)
            tier = static_cast<uint8_t>(std::min<uint32_t>(3, result->Fetch()[0].Get<uint8_t>()));

        g_donors[account] = {tier, now};
        return tier;
    }

    void SendMetadata(Player* recipient, std::string const& characterName, uint8_t tier)
    {
        if (!recipient || !recipient->GetSession())
            return;
        if (kRequireAdvertisedAddonForRecipient && !HasAddon(recipient->GetSession()))
            return;
        if (characterName.empty() || characterName.find('|') != std::string::npos)
            return;

        ChatHandler(recipient->GetSession()).SendSysMessage(
            std::string(kWirePrefix) + characterName + "|" + std::to_string(tier));
    }

    void BroadcastMetadata(std::string const& characterName, uint8_t tier)
    {
        ChatHandler handler(nullptr);
        handler.DoForAllValidSessions([&](Player* recipient)
        {
            SendMetadata(recipient, characterName, tier);
        });
    }

    class AA_DonatorPlayerScript final : public PlayerScript
    {
    public:
        AA_DonatorPlayerScript() : PlayerScript("AA_DonatorPlayerScript") { }

        void OnPlayerLogin(Player* player) override
        {
            if (!player || !player->GetSession())
                return;

            // Resolve the actual, authenticated account. No addon name check
            // is used to decide whether a player is a donor.
            uint8_t const tier = TierOf(player, true);

            // Newly logged-in clients learn other online donors. Reuse cache
            // where possible; querying on a busy bot server can be expensive.
            // Prefer that donors are discovered when they chat instead of
            // walking every bot session and querying every account here.
            if (tier != 0)
                BroadcastMetadata(player->GetName(), tier);

            // For existing cached donors, seed the new client's chat cache.
            ChatHandler handler(nullptr);
            handler.DoForAllValidSessions([&](Player* other)
            {
                if (!other || !other->GetSession() || other == player)
                    return;
                auto const found = g_donors.find(other->GetSession()->GetAccountId());
                if (found != g_donors.end() && found->second.tier != 0)
                    SendMetadata(player, other->GetName(), found->second.tier);
            });
        }

        void OnPlayerBeforeSendChatMessage(Player* player, uint32& type,
            uint32& language, std::string& message) override
        {
            (void)type;
            (void)language;
            (void)message;
            if (!player || !player->GetSession())
                return;

            // Ordinary accounts don't broadcast noisy tier-0 updates.
            // A previously granted donor is rechecked after the TTL.
            uint8_t const tier = TierOf(player);
            if (tier != 0)
                BroadcastMetadata(player->GetName(), tier);
        }
    };
}

// The CSM Modules Preview generates its own adapter.
void Addmod_aa_donatorsScripts()
{
    new AA_DonatorPlayerScript();
}
