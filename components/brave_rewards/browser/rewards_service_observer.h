/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_PAYMENTS_PAYMENTS_SERVICE_OBSERVER_H_
#define BRAVE_BROWSER_PAYMENTS_PAYMENTS_SERVICE_OBSERVER_H_

#include "base/observer_list_types.h"
#include "brave/components/brave_rewards/browser/balance_report.h"
#include "brave/components/brave_rewards/browser/content_site.h"
#include "brave/components/brave_rewards/browser/grant.h"
#include "brave/components/brave_rewards/browser/publisher_banner.h"
#include "brave/components/brave_rewards/browser/wallet_properties.h"

namespace brave_rewards {

class RewardsService;

class RewardsServiceObserver : public base::CheckedObserver {
 public:
  ~RewardsServiceObserver() override {}

  virtual void OnWalletInitialized(RewardsService* rewards_service,
                               int error_code) {};
  virtual void OnWalletProperties(
      RewardsService* rewards_service,
      int error_code,
      std::unique_ptr<brave_rewards::WalletProperties> properties) {};
  virtual void OnGrant(RewardsService* rewards_service,
                           unsigned int error_code,
                           brave_rewards::Grant properties) {};
  virtual void OnGrantCaptcha(RewardsService* rewards_service,
                              std::string image,
                              std::string hint) {};
  virtual void OnRecoverWallet(RewardsService* rewards_service,
                               unsigned int result,
                               double balance,
                               std::vector<brave_rewards::Grant> grants) {};
  virtual void OnGrantFinish(RewardsService* rewards_service,
                                 unsigned int result,
                                 brave_rewards::Grant grant) {};
  virtual void OnContentSiteUpdated(RewardsService* rewards_service) {};
  virtual void OnExcludedSitesChanged(RewardsService* rewards_service,
                                      std::string publisher_id) {};
  virtual void OnReconcileComplete(RewardsService* rewards_service,
                                   unsigned int result,
                                   const std::string& viewing_id,
                                   const std::string& category,
                                   const std::string& probi) {};
  virtual void OnRecurringDonationUpdated(RewardsService* rewards_service,
                                          brave_rewards::ContentSiteList) {};
  virtual void OnCurrentTips(RewardsService* rewards_service,
                             brave_rewards::ContentSiteList) {};
  virtual void OnPublisherBanner(brave_rewards::RewardsService* rewards_service,
                                 const brave_rewards::PublisherBanner banner) {};
  // DO NOT ADD ANY MORE METHODS HERE UNLESS IT IS A BROADCAST NOTIFICATION
  // RewardsServiceObserver should not be used to return responses to the caller.
  // Method calls on RewardsService should use callbacks to return responses.
  // The observer is primarily for broadcast notifications of events from the
  // the rewards service. OnWalletInitialized, OnContentSiteUpdated, etc...
  // are examples of events that all observers will be interested in.
};

}  // namespace brave_rewards

#endif  // BRAVE_BROWSER_PAYMENTS_PAYMENTS_SERVICE_OBSERVER_H_
