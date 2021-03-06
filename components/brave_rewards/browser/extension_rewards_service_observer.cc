/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/base64.h"
#include "brave/components/brave_rewards/browser/extension_rewards_service_observer.h"

#include "brave/common/extensions/api/brave_rewards.h"
#include "brave/components/brave_rewards/browser/rewards_service.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/event_router.h"

namespace brave_rewards {

ExtensionRewardsServiceObserver::ExtensionRewardsServiceObserver(
    Profile* profile)
    : profile_(profile) {
}

ExtensionRewardsServiceObserver::~ExtensionRewardsServiceObserver() {
}

void ExtensionRewardsServiceObserver::OnWalletInitialized(
    RewardsService* rewards_service,
    int error_code) {
  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);
  if (event_router) {
    std::unique_ptr<base::ListValue> args(new base::ListValue());
    std::unique_ptr<extensions::Event> event(new extensions::Event(
        extensions::events::BRAVE_WALLET_CREATED,
        extensions::api::brave_rewards::OnWalletCreated::kEventName,
        std::move(args)));
    event_router->BroadcastEvent(std::move(event));
  }
}

void ExtensionRewardsServiceObserver::OnWalletProperties(
    RewardsService* rewards_service,
    int error_code,
    std::unique_ptr<brave_rewards::WalletProperties> wallet_properties) {
  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);
  if (event_router && wallet_properties) {
    extensions::api::brave_rewards::OnWalletProperties::Properties properties;

    properties.probi = wallet_properties->probi;
    properties.balance = wallet_properties->balance;
    properties.rates.btc = wallet_properties->rates["BTC"];
    properties.rates.eth = wallet_properties->rates["ETH"];
    properties.rates.usd = wallet_properties->rates["USD"];
    properties.rates.eur = wallet_properties->rates["EUR"];

    for (size_t i = 0; i < wallet_properties->grants.size(); i ++) {
      properties.grants.push_back(
          extensions::api::brave_rewards::OnWalletProperties::Properties::
              GrantsType());
      auto& grant = properties.grants[properties.grants.size() -1];

      grant.altcurrency = wallet_properties->grants[i].altcurrency;
      grant.probi = wallet_properties->grants[i].probi;
      grant.expiry_time = wallet_properties->grants[i].expiryTime;
    }

    std::unique_ptr<base::ListValue> args(
        extensions::api::brave_rewards::OnWalletProperties::Create(properties)
            .release());

    std::unique_ptr<extensions::Event> event(new extensions::Event(
        extensions::events::BRAVE_ON_WALLET_PROPERTIES,
        extensions::api::brave_rewards::OnWalletProperties::kEventName,
        std::move(args)));
    event_router->BroadcastEvent(std::move(event));
  }
}

void ExtensionRewardsServiceObserver::OnGetCurrentBalanceReport(
    RewardsService* rewards_service,
    const BalanceReport& balance_report) {
  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);
  if (event_router) {
    extensions::api::brave_rewards::OnCurrentReport::Properties properties;

    properties.ads = balance_report.earning_from_ads;
    properties.closing = balance_report.closing_balance;
    properties.contribute = balance_report.auto_contribute;
    properties.deposit = balance_report.deposits;
    properties.grant = balance_report.grants;
    properties.tips = balance_report.one_time_donation;
    properties.opening = balance_report.opening_balance;
    properties.total = balance_report.total;
    properties.recurring = balance_report.recurring_donation;

    std::unique_ptr<base::ListValue> args(
        extensions::api::brave_rewards::OnCurrentReport::Create(properties)
            .release());
    std::unique_ptr<extensions::Event> event(new extensions::Event(
        extensions::events::BRAVE_ON_CURRENT_REPORT,
        extensions::api::brave_rewards::OnCurrentReport::kEventName,
        std::move(args)));
    event_router->BroadcastEvent(std::move(event));
  }
}

void ExtensionRewardsServiceObserver::OnGetPublisherActivityFromUrl(
    RewardsService* rewards_service,
    int error_code,
    std::unique_ptr<ledger::PublisherInfo> info,
    uint64_t windowId) {
  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);
  if (!event_router) {
    return;
  }

  extensions::api::brave_rewards::OnPublisherData::Publisher publisher;

  if (!info.get()) {
    info.reset(new ledger::PublisherInfo());
    info->id = "";
  }

  publisher.percentage = info->percent;
  publisher.verified = info->verified;
  publisher.excluded = info->excluded == ledger::PUBLISHER_EXCLUDE::EXCLUDED;
  publisher.name = info->name;
  publisher.url = info->url;
  publisher.provider = info->provider;
  publisher.favicon_url = info->favicon_url;
  publisher.publisher_key = info->id;
  std::unique_ptr<base::ListValue> args(
      extensions::api::brave_rewards::OnPublisherData::Create(windowId,
                                                              publisher)
          .release());

  std::unique_ptr<extensions::Event> event(new extensions::Event(
      extensions::events::BRAVE_ON_PUBLISHER_DATA,
      extensions::api::brave_rewards::OnPublisherData::kEventName,
      std::move(args)));
  event_router->BroadcastEvent(std::move(event));
}

void ExtensionRewardsServiceObserver::OnGrant(
    RewardsService* rewards_service,
    unsigned int result,
    brave_rewards::Grant grant) {
  extensions::EventRouter* event_router = extensions::EventRouter::Get(profile_);
  if (!event_router) {
    return;
  }

  base::DictionaryValue newGrant;
  newGrant.SetInteger("status", result);
  newGrant.SetString("promotionId", grant.promotionId);

  std::unique_ptr<base::ListValue> args(
      extensions::api::brave_rewards::OnGrant::Create(newGrant)
          .release());
  std::unique_ptr<extensions::Event> event(new extensions::Event(
      extensions::events::BRAVE_START,
      extensions::api::brave_rewards::OnGrant::kEventName,
      std::move(args)));
  event_router->BroadcastEvent(std::move(event));
}

void ExtensionRewardsServiceObserver::OnGrantCaptcha(
    RewardsService* rewards_service,
    std::string image,
    std::string hint) {
  extensions::EventRouter* event_router = extensions::EventRouter::Get(profile_);
  if (!event_router) {
    return;
  }

  std::string encoded_string;
  base::Base64Encode(image, &encoded_string);
  base::DictionaryValue captcha;
  captcha.SetString("image", std::move(encoded_string));
  captcha.SetString("hint", hint);

  std::unique_ptr<base::ListValue> args(
      extensions::api::brave_rewards::OnGrantCaptcha::Create(captcha)
          .release());
  std::unique_ptr<extensions::Event> event(new extensions::Event(
      extensions::events::BRAVE_START,
      extensions::api::brave_rewards::OnGrantCaptcha::kEventName,
      std::move(args)));
  event_router->BroadcastEvent(std::move(event));
}

void ExtensionRewardsServiceObserver::OnGrantFinish(
    RewardsService* rewards_service,
    unsigned int result,
    brave_rewards::Grant grant) {
  extensions::EventRouter* event_router = extensions::EventRouter::Get(profile_);
  if (!event_router) {
    return;
  }

  base::DictionaryValue finish;
  finish.SetInteger("status", result);
  finish.SetInteger("expiryTime", grant.expiryTime);
  finish.SetString("probi", grant.probi);

  std::unique_ptr<base::ListValue> args(
      extensions::api::brave_rewards::OnGrantFinish::Create(finish)
          .release());
  std::unique_ptr<extensions::Event> event(new extensions::Event(
      extensions::events::BRAVE_START,
      extensions::api::brave_rewards::OnGrantFinish::kEventName,
      std::move(args)));
  event_router->BroadcastEvent(std::move(event));
}

}  // namespace brave_rewards
