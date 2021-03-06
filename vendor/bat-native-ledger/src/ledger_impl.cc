/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <ctime>
#include <random>
#include <sstream>
#include <vector>

#include "ledger_impl.h"

#include "bat_client.h"
#include "bat_contribution.h"
#include "bat_get_media.h"
#include "bat_helper.h"
#include "bat_publishers.h"
#include "static_values.h"
#include "bat_state.h"

#include "rapidjson_bat_helper.h"

using namespace braveledger_bat_client;
using namespace braveledger_bat_publishers;
using namespace braveledger_bat_get_media;
using namespace braveledger_bat_get_media;
using namespace braveledger_bat_state;
using namespace braveledger_bat_contribution;
using namespace std::placeholders;

namespace bat_ledger {

LedgerImpl::LedgerImpl(ledger::LedgerClient* client) :
    ledger_client_(client),
    bat_client_(new BatClient(this)),
    bat_publishers_(new BatPublishers(this)),
    bat_get_media_(new BatGetMedia(this)),
    bat_state_(new BatState(this)),
    bat_contribution_(new BatContribution(this)),
    initialized_(false),
    initializing_(false),
    last_tab_active_time_(0),
    last_shown_tab_id_(-1),
    last_pub_load_timer_id_(0u),
    last_grant_check_timer_id_(0u) {
}

LedgerImpl::~LedgerImpl() {
}

void LedgerImpl::Initialize() {
  DCHECK(!initializing_);
  initializing_ = true;
  LoadLedgerState(this);
}

bool LedgerImpl::CreateWallet() {
  if (initializing_)
    return false;

  initializing_ = true;
  if (initialized_) {
    OnWalletInitialized(ledger::Result::LEDGER_ERROR);
    return false;
  }
  bat_client_->registerPersona();
  return true;
}

void LedgerImpl::AddRecurringPayment(const std::string& publisher_id, const double& value) {
  bat_publishers_->AddRecurringPayment(publisher_id, value);
}

void LedgerImpl::MakePayment(const ledger::PaymentData& payment_data) {
  bat_publishers_->MakePayment(payment_data);
}

braveledger_bat_helper::CURRENT_RECONCILE LedgerImpl::GetReconcileById(const std::string& viewingId) {
  return bat_state_->GetReconcileById(viewingId);
}

void LedgerImpl::RemoveReconcileById(const std::string& viewingId) {
  bat_state_->RemoveReconcileById(viewingId);
}

void LedgerImpl::OnLoad(const ledger::VisitData& visit_data, const uint64_t& current_time) {
  if (visit_data.domain.empty()) {
    // Skip the same domain name
    return;
  }
  visit_data_iter iter = current_pages_.find(visit_data.tab_id);
  if (iter != current_pages_.end() && iter->second.domain == visit_data.domain) {
    DCHECK(iter == current_pages_.end());
    return;
  }
  if (last_shown_tab_id_ == visit_data.tab_id) {
    last_tab_active_time_ = current_time;
  }
  current_pages_[visit_data.tab_id] = visit_data;
}

void LedgerImpl::OnUnload(uint32_t tab_id, const uint64_t& current_time) {
  OnHide(tab_id, current_time);
  visit_data_iter iter = current_pages_.find(tab_id);
  if (iter != current_pages_.end()) {
    current_pages_.erase(iter);
  }
}

void LedgerImpl::OnShow(uint32_t tab_id, const uint64_t& current_time) {
  last_tab_active_time_ = current_time;
  last_shown_tab_id_ = tab_id;
}

void LedgerImpl::OnHide(uint32_t tab_id, const uint64_t& current_time) {
  if (tab_id != last_shown_tab_id_) {
    return;
  }
  visit_data_iter iter = current_pages_.find(tab_id);
  if (iter == current_pages_.end() || 0 == last_tab_active_time_) {
    return;
  }
  DCHECK(last_tab_active_time_);
  bat_publishers_->saveVisit(iter->second.tld, iter->second, current_time - last_tab_active_time_);
  last_tab_active_time_ = 0;
}

void LedgerImpl::OnForeground(uint32_t tab_id, const uint64_t& current_time) {
  // TODO media resources could have been played in the background
  if (last_shown_tab_id_ != tab_id) {
    return;
  }
  OnShow(tab_id, current_time);
}

void LedgerImpl::OnBackground(uint32_t tab_id, const uint64_t& current_time) {
  // TODO media resources could stay and be active in the background
  OnHide(tab_id, current_time);
}

void LedgerImpl::OnMediaStart(uint32_t tab_id, const uint64_t& current_time) {
  // TODO
}

void LedgerImpl::OnMediaStop(uint32_t tab_id, const uint64_t& current_time) {
  // TODO
}

void LedgerImpl::OnXHRLoad(
    uint32_t tab_id,
    const std::string& url,
    const std::map<std::string, std::string>& parts,
    const std::string& first_party_url,
    const std::string& referrer,
    const ledger::VisitData& visit_data) {
  std::string type = bat_get_media_->GetLinkType(url, first_party_url, referrer);
  if (type.empty()) {
    // It is not a media supported type
    return;
  }
  bat_get_media_->processMedia(parts, type, visit_data);
}

void LedgerImpl::OnPostData(
      const std::string& url,
      const std::string& first_party_url,
      const std::string& referrer,
      const std::string& post_data,
      const ledger::VisitData& visit_data) {
  std::string type = bat_get_media_->GetLinkType(url, first_party_url, referrer);
  if (type.empty()) {
     // It is not a media supported type
    return;
  }
  std::vector<std::map<std::string, std::string>> twitchParts;
  if (TWITCH_MEDIA_TYPE == type) {
    braveledger_bat_helper::getTwitchParts(post_data, twitchParts);
    for (size_t i = 0; i < twitchParts.size(); i++) {
      bat_get_media_->processMedia(twitchParts[i], type, visit_data);
    }
  }
}

void LedgerImpl::LoadLedgerState(ledger::LedgerCallbackHandler* handler) {
  ledger_client_->LoadLedgerState(handler);
}

void LedgerImpl::OnLedgerStateLoaded(ledger::Result result,
                                        const std::string& data) {
  if (result == ledger::Result::LEDGER_OK) {
    if (!bat_state_->LoadState(data)) {
      BLOG(this, ledger::LogLevel::LOG_ERROR) <<
        "Successfully loaded but failed to parse ledger state.";
      BLOG(this, ledger::LogLevel::LOG_DEBUG) <<
        "Failed ledger state: " << data;

      OnWalletInitialized(ledger::Result::INVALID_LEDGER_STATE);
    } else {
      LoadPublisherState(this);
      bat_contribution_->OnStartUp();
    }
  } else {
    BLOG(this, ledger::LogLevel::LOG_ERROR) << "Failed to load ledger state";
    BLOG(this, ledger::LogLevel::LOG_DEBUG) << "Failed ledger state: " << data;

    OnWalletInitialized(result);
  }
}

void LedgerImpl::LoadPublisherState(ledger::LedgerCallbackHandler* handler) {
  ledger_client_->LoadPublisherState(handler);
}

void LedgerImpl::OnPublisherStateLoaded(ledger::Result result,
                                        const std::string& data) {
  if (result == ledger::Result::LEDGER_OK) {
    if (!bat_publishers_->loadState(data)) {
      BLOG(this, ledger::LogLevel::LOG_ERROR) <<
        "Successfully loaded but failed to parse ledger state.";
      BLOG(this, ledger::LogLevel::LOG_DEBUG) <<
        "Failed publisher state: " << data;

      result = ledger::Result::INVALID_PUBLISHER_STATE;
    }
  } else {
    BLOG(this, ledger::LogLevel::LOG_ERROR) <<
      "Failed to load publisher state";
      BLOG(this, ledger::LogLevel::LOG_DEBUG) <<
        "Failed publisher state: " << data;
  }

  OnWalletInitialized(result);
}

void LedgerImpl::SaveLedgerState(const std::string& data) {
  ledger_client_->SaveLedgerState(data, this);
}

void LedgerImpl::SavePublisherState(const std::string& data,
                                    ledger::LedgerCallbackHandler* handler) {
  ledger_client_->SavePublisherState(data, handler);
}


void LedgerImpl::SavePublishersList(const std::string& data) {
  ledger_client_->SavePublishersList(data, this);
}

void LedgerImpl::LoadPublisherList(ledger::LedgerCallbackHandler* handler) {
  ledger_client_->LoadPublisherList(handler);
}

void LedgerImpl::OnPublisherListLoaded(ledger::Result result,
                                       const std::string& data) {
  if (result == ledger::Result::LEDGER_OK) {
    if (!bat_publishers_->loadPublisherList(data)) {
      BLOG(this, ledger::LogLevel::LOG_ERROR) <<
        "Successfully loaded but failed to parse publish list.";
      BLOG(this, ledger::LogLevel::LOG_DEBUG) <<
        "Failed publisher list: " << data;
    }
  } else {
    BLOG(this, ledger::LogLevel::LOG_ERROR) <<
      "Failed to load publisher list";
    BLOG(this, ledger::LogLevel::LOG_DEBUG) <<
      "Failed publisher list: " << data;
  }

  RefreshPublishersList(false);
}

std::string LedgerImpl::GenerateGUID() const {
  return ledger_client_->GenerateGUID();
}

void LedgerImpl::OnWalletInitialized(ledger::Result result) {
  initializing_ = false;
  ledger_client_->OnWalletInitialized(result);

  if (result == ledger::Result::LEDGER_OK || result == ledger::Result::WALLET_CREATED) {
    initialized_ = true;
    LoadPublisherList(this);
    bat_contribution_->SetReconcileTimer();
    RefreshGrant(false);
  } else {
    BLOG(this, ledger::LogLevel::LOG_ERROR) << "Failed to initialize wallet";
  }
}

void LedgerImpl::LoadURL(const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& content,
    const std::string& contentType,
    const ledger::URL_METHOD& method,
    ledger::LoadURLCallback callback) {
  ledger_client_->LoadURL(
      url, headers, content, contentType, method, callback);
}

std::string LedgerImpl::URIEncode(const std::string& value) {
  return ledger_client_->URIEncode(value);
}

void LedgerImpl::SetPublisherInfo(std::unique_ptr<ledger::PublisherInfo> info,
                                  ledger::PublisherInfoCallback callback) {
  ledger_client_->SavePublisherInfo(std::move(info),
      std::bind(&LedgerImpl::OnSetPublisherInfo, this, callback, _1, _2));
}

void LedgerImpl::SetMediaPublisherInfo(const std::string& media_key,
                                const std::string& publisher_id) {
  if (!media_key.empty() && !publisher_id.empty()) {
    ledger_client_->SaveMediaPublisherInfo(media_key, publisher_id);
  }
}

void LedgerImpl::SaveMediaVisit(const std::string& publisher_id,
                                const ledger::VisitData& visit_data,
                                const uint64_t& duration,
                                const uint64_t window_id) {
  if (bat_publishers_->getPublisherAllowVideos()) {
    bat_publishers_->saveVisit(publisher_id, visit_data, duration);
  }
}

void LedgerImpl::SetPublisherExclude(const std::string& publisher_id, const ledger::PUBLISHER_EXCLUDE& exclude) {
  bat_publishers_->setExclude(publisher_id, exclude);
}

void LedgerImpl::SetPublisherPanelExclude(const std::string& publisher_id,
  const ledger::PUBLISHER_EXCLUDE& exclude, uint64_t windowId) {
  bat_publishers_->setPanelExclude(publisher_id, exclude, windowId);
}

void LedgerImpl::RestorePublishers() {
  bat_publishers_->restorePublishers();
}

void LedgerImpl::LoadNicewareList(ledger::GetNicewareListCallback callback) {
  ledger_client_->LoadNicewareList(callback);
}

void LedgerImpl::OnSetPublisherInfo(ledger::PublisherInfoCallback callback,
                                    ledger::Result result,
                                    std::unique_ptr<ledger::PublisherInfo> info) {
  info = bat_publishers_->onPublisherInfoUpdated(result, std::move(info));
  callback(result, std::move(info));
}

std::vector<ledger::ContributionInfo> LedgerImpl::GetRecurringDonationPublisherInfo() {
  return bat_publishers_->GetRecurringDonationList();
}

void LedgerImpl::GetPublisherInfo(
    const ledger::PublisherInfoFilter& filter,
    ledger::PublisherInfoCallback callback) {
  ledger_client_->LoadPublisherInfo(filter, callback);
}

void LedgerImpl::GetMediaPublisherInfo(const std::string& media_key,
                                ledger::PublisherInfoCallback callback) {
  ledger_client_->LoadMediaPublisherInfo(media_key, callback);
}

void LedgerImpl::GetPublisherInfoList(uint32_t start, uint32_t limit,
                                const ledger::PublisherInfoFilter& filter,
                                ledger::PublisherInfoListCallback callback) {
  ledger_client_->LoadPublisherInfoList(start, limit, filter, callback);
}

void LedgerImpl::SetRewardsMainEnabled(bool enabled) {
  bat_state_->SetRewardsMainEnabled(enabled);
}

void LedgerImpl::SetPublisherMinVisitTime(uint64_t duration) { // In seconds
  bat_publishers_->setPublisherMinVisitTime(duration);
}

void LedgerImpl::SetPublisherMinVisits(unsigned int visits) {
  bat_publishers_->setPublisherMinVisits(visits);
}

void LedgerImpl::SetPublisherAllowNonVerified(bool allow) {
  bat_publishers_->setPublisherAllowNonVerified(allow);
}

void LedgerImpl::SetPublisherAllowVideos(bool allow) {
  bat_publishers_->setPublisherAllowVideos(allow);
}

void LedgerImpl::SetContributionAmount(double amount) {
  bat_state_->SetContributionAmount(amount);
}

void LedgerImpl::SetUserChangedContribution() {
  bat_state_->SetUserChangedContribution();
}

bool LedgerImpl::GetUserChangedContribution() {
  return bat_state_->GetUserChangedContribution();
}

void LedgerImpl::SetAutoContribute(bool enabled) {
  bat_state_->SetAutoContribute(enabled);
}

void LedgerImpl::GetAutoContributeProps(ledger::AutoContributeProps& props) {
  props.enabled_contribute = GetAutoContribute();
  props.contribution_min_time = GetPublisherMinVisitTime();
  props.contribution_min_visits = GetPublisherMinVisits();
  props.contribution_non_verified = GetPublisherAllowNonVerified();
  props.contribution_videos = GetPublisherAllowVideos();
  props.reconcile_stamp = GetReconcileStamp();
}

bool LedgerImpl::GetRewardsMainEnabled() const {
  return bat_state_->GetRewardsMainEnabled();
}

uint64_t LedgerImpl::GetPublisherMinVisitTime() const {
  return bat_publishers_->getPublisherMinVisitTime();
}

unsigned int LedgerImpl::GetPublisherMinVisits() const {
  return bat_publishers_->getPublisherMinVisits();
}

unsigned int LedgerImpl::GetNumExcludedSites() const {
  return bat_publishers_->getNumExcludedSites();
}

bool LedgerImpl::GetPublisherAllowNonVerified() const {
  return bat_publishers_->getPublisherAllowNonVerified();
}

bool LedgerImpl::GetPublisherAllowVideos() const {
  return bat_publishers_->getPublisherAllowVideos();
}

double LedgerImpl::GetContributionAmount() const {
  return bat_state_->GetContributionAmount();
}

bool LedgerImpl::GetAutoContribute() const {
  return bat_state_->GetAutoContribute();
}

std::map<std::string, std::string> LedgerImpl::GetAddresses() {
  std::map<std::string, std::string> addresses;
  addresses.emplace("BAT", GetBATAddress());
  addresses.emplace("BTC", GetBTCAddress());
  addresses.emplace("ETH", GetETHAddress());
  addresses.emplace("LTC", GetLTCAddress());
  return addresses;
}

const std::string& LedgerImpl::GetBATAddress() const {
  return bat_state_->GetBATAddress();
}

const std::string& LedgerImpl::GetBTCAddress() const {
  return bat_state_->GetBTCAddress();
}

const std::string& LedgerImpl::GetETHAddress() const {
  return bat_state_->GetETHAddress();
}

const std::string& LedgerImpl::GetLTCAddress() const {
  return bat_state_->GetLTCAddress();
}

uint64_t LedgerImpl::GetReconcileStamp() const {
  return bat_state_->GetReconcileStamp();
}

void LedgerImpl::OnReconcileComplete(ledger::Result result,
                                    const std::string& viewing_id,
                                    const std::string& probi) {
  auto reconcile = GetReconcileById(viewing_id);

  ledger_client_->OnReconcileComplete(
      result,
      viewing_id,
      (ledger::PUBLISHER_CATEGORY)reconcile.category_,
      probi);
}

void LedgerImpl::OnWalletProperties(ledger::Result result,
    const braveledger_bat_helper::WALLET_PROPERTIES_ST& properties) {
  std::unique_ptr<ledger::WalletInfo> info;

  if (result == ledger::Result::LEDGER_OK) {
    info.reset(new ledger::WalletInfo);
    info->altcurrency_ = properties.altcurrency_;
    info->probi_ = properties.probi_;
    info->balance_ = properties.balance_;
    info->rates_ = properties.rates_;
    info->parameters_choices_ = properties.parameters_choices_;
    info->fee_amount_ = bat_state_->GetContributionAmount();
    info->parameters_range_ = properties.parameters_range_;
    info->parameters_days_ = properties.parameters_days_;

    for (size_t i = 0; i < properties.grants_.size(); i ++) {
      ledger::Grant grant;

      grant.altcurrency = properties.grants_[i].altcurrency;
      grant.probi = properties.grants_[i].probi;
      grant.expiryTime = properties.grants_[i].expiryTime;

      info->grants_.push_back(grant);
    }
  }

  ledger_client_->OnWalletProperties(result, std::move(info));
}

void LedgerImpl::FetchWalletProperties() const {
  bat_client_->getWalletProperties();
}

void LedgerImpl::FetchGrant(const std::string& lang,
                              const std::string& payment_id) const {
  bat_client_->getGrant(lang, payment_id);
}

void LedgerImpl::OnGrant(ledger::Result result, const braveledger_bat_helper::GRANT& properties) {
  ledger::Grant grant;

  grant.promotionId = properties.promotionId;
  last_grant_check_timer_id_ = 0;
  RefreshGrant(result != ledger::Result::LEDGER_OK &&
    result != ledger::Result::GRANT_NOT_FOUND);
  ledger_client_->OnGrant(result, grant);
}

void LedgerImpl::GetGrantCaptcha() const {
  bat_client_->getGrantCaptcha();
}

void LedgerImpl::OnGrantCaptcha(const std::string& image, const std::string& hint) {
  ledger_client_->OnGrantCaptcha(image, hint);
}

std::string LedgerImpl::GetWalletPassphrase() const {
  return bat_client_->getWalletPassphrase();
}

void LedgerImpl::RecoverWallet(const std::string& passPhrase) const {
  bat_client_->recoverWallet(passPhrase);
}

void LedgerImpl::OnRecoverWallet(ledger::Result result, double balance, const std::vector<braveledger_bat_helper::GRANT>& grants) {
  if (result != ledger::Result::LEDGER_OK) {
    BLOG(this, ledger::LogLevel::LOG_ERROR) << "Failed to recover wallet";
  }

  std::vector<ledger::Grant> ledgerGrants;

  for (size_t i = 0; i < grants.size(); i ++) {
    ledger::Grant tempGrant;

    tempGrant.altcurrency = grants[i].altcurrency;
    tempGrant.probi = grants[i].probi;
    tempGrant.expiryTime = grants[i].expiryTime;

    ledgerGrants.push_back(tempGrant);
  }
  if (result == ledger::Result::LEDGER_OK) {
    bat_publishers_->clearAllBalanceReports();
  }

  ledger_client_->OnRecoverWallet(result ? ledger::Result::LEDGER_ERROR :
                                          ledger::Result::LEDGER_OK,
                                  balance,
                                  ledgerGrants);
}

void LedgerImpl::SolveGrantCaptcha(const std::string& solution) const {
  bat_client_->setGrant(solution, "");
}

void LedgerImpl::OnGrantFinish(ledger::Result result, const braveledger_bat_helper::GRANT& grant) {
  ledger::Grant newGrant;

  newGrant.altcurrency = grant.altcurrency;
  newGrant.probi = grant.probi;
  newGrant.expiryTime = grant.expiryTime;
  newGrant.promotionId = grant.promotionId;

  ledger_client_->OnGrantFinish(result, newGrant);
}

bool LedgerImpl::GetBalanceReport(ledger::PUBLISHER_MONTH month,
                                int year,
                                ledger::BalanceReportInfo* report_info) const {
  return bat_publishers_->getBalanceReport(month, year, report_info);
}

std::map<std::string, ledger::BalanceReportInfo> LedgerImpl::GetAllBalanceReports() const {
  return bat_publishers_->getAllBalanceReports();
}

void LedgerImpl::SetBalanceReport(ledger::PUBLISHER_MONTH month,
                                int year,
                                const ledger::BalanceReportInfo& report_info) {
  bat_publishers_->setBalanceReport(month, year, report_info);
}

void LedgerImpl::DoDirectDonation(const ledger::PublisherInfo& publisher, const int amount, const std::string& currency) {
  if (publisher.id.empty()) {
    BLOG(this, ledger::LogLevel::LOG_ERROR) <<
      "Failed direct donation due to missing publisher id";

    // TODO add error flow
    return;
  }

  auto direction = braveledger_bat_helper::RECONCILE_DIRECTION(publisher.id, amount, currency);
  auto direction_list = std::vector<braveledger_bat_helper::RECONCILE_DIRECTION> { direction };
  braveledger_bat_helper::PublisherList list;
  bat_contribution_->StartReconcile(GenerateGUID(),
                         ledger::PUBLISHER_CATEGORY::DIRECT_DONATION,
                         list,
                         direction_list);
}

void LedgerImpl::OnTimer(uint32_t timer_id) {
  if (timer_id == last_pub_load_timer_id_) {
    last_pub_load_timer_id_ = 0;

    //download the list
    std::string url = braveledger_bat_helper::buildURL(GET_PUBLISHERS_LIST_V1, "", braveledger_bat_helper::SERVER_TYPES::PUBLISHER);
    auto callback = std::bind(&LedgerImpl::LoadPublishersListCallback, this,
        _1, _2, _3);
    LoadURL(url, std::vector<std::string>(), "", "",
        ledger::URL_METHOD::GET, callback);
  } else if (timer_id == last_grant_check_timer_id_) {
    last_grant_check_timer_id_ = 0;
    FetchGrant(std::string(), std::string());
  }

  bat_contribution_->OnTimer(timer_id);
}

void LedgerImpl::GetRecurringDonations(ledger::PublisherInfoListCallback callback) {
  ledger_client_->GetRecurringDonations(callback);
}

void LedgerImpl::LoadPublishersListCallback(bool result, const std::string& response, const std::map<std::string, std::string>& headers) {
  if (result && !response.empty()) {
    bat_publishers_->RefreshPublishersList(response);
  } else {
    BLOG(this, ledger::LogLevel::LOG_ERROR) <<
      "Can't fetch publisher list";
    //error: retry downloading again
    RefreshPublishersList(true);
  }
}

void LedgerImpl::RefreshPublishersList(bool retryAfterError) {
  uint64_t start_timer_in{ 0ull };

  if (last_pub_load_timer_id_ != 0) {
    //timer in progress
    return;
  }

  if (retryAfterError) {
    start_timer_in = retryRequestSetup(300, 3600);

    BLOG(this, ledger::LogLevel::LOG_WARNING) <<
      "Failed to refresh publishesr list, will try again in " << start_timer_in;
  }
  else {
    uint64_t now = std::time(nullptr);
    uint64_t lastLoadTimestamp = bat_publishers_->getLastPublishersListLoadTimestamp();

    //check if lastLoadTimestamp doesn't exist or have erroneous value.
    //(start_timer_in == 0) is expected to call callback function immediately.

    //time since last successful download
    uint64_t  time_since_last_download = (lastLoadTimestamp == 0ull || lastLoadTimestamp > now) ? 0ull : now - lastLoadTimestamp;

    if (now == lastLoadTimestamp) {
      start_timer_in = braveledger_ledger::_publishers_list_load_interval;
    }
    else if (time_since_last_download > 0 && time_since_last_download < braveledger_ledger::_publishers_list_load_interval) {
      start_timer_in = braveledger_ledger::_publishers_list_load_interval - time_since_last_download;
    }
    else {
      start_timer_in = 0ull;
    }
  }

  //start timer
  SetTimer(start_timer_in, last_pub_load_timer_id_);
}

void LedgerImpl::RefreshGrant(bool retryAfterError) {
  uint64_t start_timer_in{ 0ull };
  if (last_grant_check_timer_id_ != 0) {
    return;
  }

  if (retryAfterError) {
    start_timer_in = retryRequestSetup(300, 600);

    BLOG(this, ledger::LogLevel::LOG_WARNING) <<
      "Failed to refresh grant, will try again in " << start_timer_in;
  } else {
    uint64_t now = std::time(nullptr);
    uint64_t last_grant_stamp = bat_state_->GetLastGrantLoadTimestamp();

    uint64_t time_since_last_grant_check = (last_grant_stamp == 0ull ||
      last_grant_stamp > now) ? 0ull : now - last_grant_stamp;
    if (now == last_grant_stamp) {
      start_timer_in = braveledger_ledger::_grant_load_interval;
    } else if (time_since_last_grant_check > 0 &&
      time_since_last_grant_check < braveledger_ledger::_grant_load_interval) {
      start_timer_in =
        braveledger_ledger::_grant_load_interval - time_since_last_grant_check;
    } else {
      start_timer_in = 0ull;
    }
  }
  SetTimer(start_timer_in, last_grant_check_timer_id_);
}

uint64_t LedgerImpl::retryRequestSetup(uint64_t min_time, uint64_t max_time) {
  std::random_device seeder;
  const auto seed = seeder.entropy() ? seeder() : time(nullptr);
  std::mt19937 eng(static_cast<std::mt19937::result_type>(seed));
  DCHECK(max_time > min_time);
  std::uniform_int_distribution <> dist(min_time, max_time);
  return dist(eng);
}

void LedgerImpl::OnPublishersListSaved(ledger::Result result) {
  bool retryAfterError = !(ledger::Result::LEDGER_OK == result);
  bat_publishers_->OnPublishersListSaved(result);
  RefreshPublishersList(retryAfterError);
}

bool LedgerImpl::IsWalletCreated() const {
  return bat_state_->IsWalletCreated();
}

void LedgerImpl::GetPublisherActivityFromUrl(uint64_t windowId,
                                             const ledger::VisitData& visit_data) {
  bat_publishers_->getPublisherActivityFromUrl(windowId, visit_data);
}

void LedgerImpl::GetMediaActivityFromUrl(uint64_t windowId,
                                         const ledger::VisitData& visit_data,
                                         const std::string& providerType) {
  bat_get_media_->getMediaActivityFromUrl(windowId, visit_data, providerType);
}

void LedgerImpl::OnPublisherActivity(ledger::Result result,
                                        std::unique_ptr<ledger::PublisherInfo> info, uint64_t windowId) {
  ledger_client_->OnPublisherActivity(result, std::move(info), windowId);
}

void LedgerImpl::OnExcludedSitesChanged(const std::string& publisher_id) {
  ledger_client_->OnExcludedSitesChanged(publisher_id);
}

void LedgerImpl::SetBalanceReportItem(ledger::PUBLISHER_MONTH month,
                                      int year,
                                      ledger::ReportType type,
                                      const std::string& probi) {
  bat_publishers_->setBalanceReportItem(month, year, type, probi);
}

void LedgerImpl::FetchFavIcon(const std::string& url,
                              const std::string& favicon_key,
                              ledger::FetchIconCallback callback) {
  ledger_client_->FetchFavIcon(url, favicon_key, callback);
}

void LedgerImpl::GetPublisherBanner(const std::string& publisher_id,
                                    ledger::PublisherBannerCallback callback) {
  bat_publishers_->getPublisherBanner(publisher_id, callback);
}

double LedgerImpl::GetBalance() {
  return bat_state_->GetBalance();
}

void LedgerImpl::OnReconcileCompleteSuccess(const std::string& viewing_id,
                                            const ledger::PUBLISHER_CATEGORY category,
                                            const std::string& probi,
                                            const ledger::PUBLISHER_MONTH month,
                                            const int year,
                                            const uint32_t date) {

  bat_contribution_->OnReconcileCompleteSuccess(viewing_id,
                                                category,
                                                probi,
                                                month,
                                                year,
                                                date);
}

void LedgerImpl::RemoveRecurring(const std::string& publisher_key) {
  ledger_client_->OnRemoveRecurring(publisher_key, std::bind(&LedgerImpl::OnRemovedRecurring,
                                        this,
                                        _1));
}

void LedgerImpl::OnRemovedRecurring(ledger::Result result) {
  if (result != ledger::Result::LEDGER_OK) {
    BLOG(this, ledger::LogLevel::LOG_ERROR) <<
      "Failed to remove recurring";

    // TODO add error callback
    return;
  }
}

ledger::PublisherInfoFilter LedgerImpl::CreatePublisherFilter(
    const std::string& publisher_id,
    ledger::PUBLISHER_CATEGORY category,
    ledger::PUBLISHER_MONTH month,
    int year,
    ledger::PUBLISHER_EXCLUDE_FILTER excluded,
    bool min_duration,
    const uint64_t& currentReconcileStamp,
    bool non_verified) {
  return bat_publishers_->CreatePublisherFilter(publisher_id,
                                                category,
                                                month,
                                                year,
                                                excluded,
                                                min_duration,
                                                currentReconcileStamp,
                                                non_verified);
}


std::unique_ptr<ledger::LogStream> LedgerImpl::Log(
  const char* file,
  int line,
  const ledger::LogLevel log_level) const {

  // TODO(Terry Mancey): bat-native-ledger architecture does not expose the
  // client however the ledger impl is exposed so for now we will proxy logging
  // via from the ledger impl to the client
  return ledger_client_->Log(file, line, log_level);
}

void LedgerImpl::LogResponse(const std::string& func_name,
                             bool result,
                             const std::string& response,
                             const std::map<std::string, std::string>& headers) {
  std::string stat = result ? "Success" : "Failure";

  std::string formatted_headers = "";
  for (auto header = headers.begin(); header != headers.end(); ++header) {
    formatted_headers += "> headers " + header->first + ": " + header->second;
    if (header != headers.end()) {
      formatted_headers += "\n";
    }
  }

  BLOG(this, ledger::LogLevel::LOG_RESPONSE) << std::endl
    << "[ RESPONSE - " << func_name << " ]" << std::endl
    << "> time: " << std::time(nullptr) << std::endl
    << "> result: " << stat << std::endl
    << "> response: " << response
    << formatted_headers
    << "[ END RESPONSE ]";
}

void LedgerImpl::ResetReconcileStamp() {
  bat_state_->ResetReconcileStamp();
}

bool LedgerImpl::UpdateReconcile(
    const braveledger_bat_helper::CURRENT_RECONCILE& reconcile) {
  return bat_state_->UpdateReconcile(reconcile);
}

void LedgerImpl::AddReconcile(
      const std::string& viewing_id,
      const braveledger_bat_helper::CURRENT_RECONCILE& reconcile) {
  bat_state_->AddReconcile(viewing_id, reconcile);
}

const std::string& LedgerImpl::GetPaymentId() const {
  return bat_state_->GetPaymentId();
}

void LedgerImpl::SetPaymentId(const std::string& payment_id) {
  bat_state_->SetPaymentId(payment_id);
}

const braveledger_bat_helper::GRANT& LedgerImpl::GetGrant() const {
  return bat_state_->GetGrant();
}

void LedgerImpl::SetGrant(braveledger_bat_helper::GRANT grant) {
  bat_state_->SetGrant(grant);
}

const std::string& LedgerImpl::GetPersonaId() const {
  return bat_state_->GetPersonaId();
}

void LedgerImpl::SetPersonaId(const std::string& persona_id) {
  bat_state_->SetPersonaId(persona_id);
}

const std::string& LedgerImpl::GetUserId() const {
  return bat_state_->GetUserId();
}

void LedgerImpl::SetUserId(const std::string& user_id) {
  bat_state_->SetUserId(user_id);
}

const std::string& LedgerImpl::GetRegistrarVK() const {
  return bat_state_->GetRegistrarVK();
}

void LedgerImpl::SetRegistrarVK(const std::string& registrar_vk) {
  bat_state_->SetRegistrarVK(registrar_vk);
}

const std::string& LedgerImpl::GetPreFlight() const {
  return bat_state_->GetPreFlight();
}

void LedgerImpl::SetPreFlight(const std::string& pre_flight) {
  bat_state_->SetPreFlight(pre_flight);
}

const braveledger_bat_helper::WALLET_INFO_ST&
LedgerImpl::GetWalletInfo() const {
  return bat_state_->GetWalletInfo();
}

void LedgerImpl::SetWalletInfo(
    const braveledger_bat_helper::WALLET_INFO_ST& info) {
  bat_state_->SetWalletInfo(info);
}

const braveledger_bat_helper::WALLET_PROPERTIES_ST&
LedgerImpl::GetWalletProperties() const {
  return bat_state_->GetWalletProperties();
}

void LedgerImpl::SetWalletProperties(
    braveledger_bat_helper::WALLET_PROPERTIES_ST& properties) {
  bat_state_->SetWalletProperties(properties);
}

unsigned int LedgerImpl::GetDays() const {
  return bat_state_->GetDays();
}

void LedgerImpl::SetDays(unsigned int days) {
  bat_state_->SetDays(days);
}

const braveledger_bat_helper::Transactions&
LedgerImpl::GetTransactions() const {
  return bat_state_->GetTransactions();
}

void LedgerImpl::SetTransactions(
    const braveledger_bat_helper::Transactions& transactions) {
  bat_state_->SetTransactions(transactions);
}

const braveledger_bat_helper::Ballots& LedgerImpl::GetBallots() const {
  return bat_state_->GetBallots();
}

void LedgerImpl::SetBallots(const braveledger_bat_helper::Ballots& ballots) {
  bat_state_->SetBallots(ballots);
}

const braveledger_bat_helper::BatchVotes& LedgerImpl::GetBatch() const {
  return bat_state_->GetBatch();
}

void LedgerImpl::SetBatch(const braveledger_bat_helper::BatchVotes& votes) {
  bat_state_->SetBatch(votes);
}

const std::string& LedgerImpl::GetCurrency() const {
  return bat_state_->GetCurrency();
}

void LedgerImpl::SetCurrency(const std::string& currency) {
  bat_state_->SetCurrency(currency);
}

void LedgerImpl::SetLastGrantLoadTimestamp(uint64_t stamp) {
  bat_state_->SetLastGrantLoadTimestamp(stamp);
}

uint64_t LedgerImpl::GetBootStamp() const {
  return bat_state_->GetBootStamp();
}

void LedgerImpl::SetBootStamp(uint64_t stamp) {
  bat_state_->SetBootStamp(stamp);
}

const std::string& LedgerImpl::GetMasterUserToken() const {
  return bat_state_->GetMasterUserToken();
}

void LedgerImpl::SetMasterUserToken(const std::string& token) {
  bat_state_->SetMasterUserToken(token);
}

bool LedgerImpl::ReconcileExists(const std::string& viewingId) {
  return bat_state_->ReconcileExists(viewingId);
}

void LedgerImpl::SaveContributionInfo(const std::string& probi,
    const int month,
    const int year,
    const uint32_t date,
    const std::string& publisher_key,
    const ledger::PUBLISHER_CATEGORY category) {
  ledger_client_->SaveContributionInfo(probi,
                                       month,
                                       year,
                                       date,
                                       publisher_key,
                                       category);
}

void LedgerImpl::NormalizeContributeWinners(
    ledger::PublisherInfoList* newList,
    bool saveData,
    const braveledger_bat_helper::PublisherList& list,
    uint32_t record) {
  bat_publishers_->NormalizeContributeWinners(newList, saveData, list, record);
}

void LedgerImpl::SetTimer(uint64_t time_offset, uint32_t& timer_id) const {
  ledger_client_->SetTimer(time_offset, timer_id);
}

bool LedgerImpl::AddReconcileStep(const std::string& viewing_id,
                                  braveledger_bat_helper::ContributionRetry step,
                                  int level) {

  BLOG(this, ledger::LogLevel::LOG_DEBUG) << "Contribution step"
                                          << std::to_string(step)
                                          << "for" <<viewing_id;
  return bat_state_->AddReconcileStep(viewing_id, step, level);
}

const braveledger_bat_helper::CurrentReconciles&
LedgerImpl::GetCurrentReconciles() const {
  return bat_state_->GetCurrentReconciles();
}

double LedgerImpl::GetDefaultContributionAmount() {
  return bat_state_->GetDefaultContributionAmount();
}

bool LedgerImpl::HasSufficientBalanceToReconcile() {
  return GetBalance() >= GetContributionAmount();
}

}  // namespace bat_ledger
