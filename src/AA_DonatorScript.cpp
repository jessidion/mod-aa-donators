#include "Chat.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace
{
    // On the host world thread. The core handles dispatching PlayerScript hooks.
    constexpr auto kCacheLifetime = std::chrono::seconds(45);
    constexpr char kAddonName[] = "AADonatorIcons";
    constexpr char kWirePrefix[] = "AADONOR1|";
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
        for (std::string const& name : session->GetClientAddonNames())
            if (name == kAddonName)
                return true;
        return false;
    }

    uint8_t TierOf(Player* player, bool force = false)
    {
        if (!player || !player->GetSession())
            return 0;
        uint32_t account = player->GetSession()->GetAccountId();
        auto now = std::chrono::steady_clock::now();
        auto found = g_donors.find(account);
        if (!force && found != g_donors.end() && now - found->second.checked < kCacheLifetime)
            return found->second.tier;

        uint8_t tier = 0;
        // account is an integer obtained from the authenticated server session, not user input.
        // Missing row = ordinary player; higher unknown tiers are clamped to Ascended.
        auto result = LoginDatabase.Query("SELECT donator FROM aa_donators WHERE account_id = {} LIMIT 1", account);
        if (result)
            tier = static_cast<uint8_t>(std::min<uint32_t>(3, result->Fetch()[0].Get<uint8_t>()));
        g_donors[account] = { tier, now };
        return tier;
    }

    void SendMetadata(Player* recipient, std::string const& characterName, uint8_t tier)
    {
        if (!recipient || !recipient->GetSession() || !HasAddon(recipient->GetSession()))
            return;
        if (characterName.empty() || characterName.find('|') != std::string::npos)
            return;
        // Server-produced system chat cannot be impersonated by a normal player message.
        // The client addon consumes this record before it reaches the chat frame.
        ChatHandler(recipient->GetSession()).SendSysMessage(
            std::string(kWirePrefix) + characterName + "|" + std::to_string(tier));
    }

    void BroadcastMetadata(std::string const& characterName, uint8_t tier)
    {
        // Use AzerothCore's valid-session iterator: no raw player-container access.
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

            // Playerbots generally have no client addon: don't query DB for each bot.
            if (!HasAddon(player->GetSession()))
                return;

            // Populate the new client's cache with already-known online donors.
            // Only accounts that have our addon are looked up, avoiding the bot population.
            ChatHandler handler(nullptr);
            handler.DoForAllValidSessions([&](Player* other)
            {
                if (!other || !other->GetSession() || !HasAddon(other->GetSession()))
                    return;
                uint8_t tier = TierOf(other);
                if (tier != 0)
                    SendMetadata(player, other->GetName(), tier);
            });
            BroadcastMetadata(player->GetName(), TierOf(player, true));
        }

        void OnPlayerLogout(Player* player) override
        {
            // Account tier cache remains for 45 seconds to avoid needless MySQL requests
            // if another character on the same account logs in immediately.
            (void)player;
        }

        void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& language, std::string& message) override
        {
            (void)type;
            (void)language;
            (void)message;
            if (!player || !player->GetSession() || !HasAddon(player->GetSession()))
                return;
            // Recheck every 45 s so Stripe webhook updates become visible without relogging.
            // Broadcasting a metadata record immediately before chat allows clients to decorate
            // the following server-formatted player hyperlink without rewriting the message body.
            BroadcastMetadata(player->GetName(), TierOf(player));
        }
    };

}

// Modules Preview's controlled recipe generates its own adapter and calls this symbol.
// When compiled standalone, the adapter in standalone/ invokes the same function.
void Addmod_aa_donatorsScripts()
{
    new AA_DonatorPlayerScript();
}
