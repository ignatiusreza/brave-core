/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/services/bat_ledger/bat_ledger_client_mojo_proxy.h"

#include "base/logging.h"
#include "mojo/public/cpp/bindings/map.h"

namespace bat_ledger {

namespace {

int32_t ToMojomResult(ledger::Result result) {
  return (int32_t)result;
}

ledger::Result ToLedgerResult(int32_t result) {
  return (ledger::Result)result;
}

int32_t ToMojomPublisherCategory(ledger::PUBLISHER_CATEGORY category) {
  return (int32_t)category;
}

int32_t ToMojomMethod(ledger::URL_METHOD method) {
  return (int32_t)method;
}

class LogStreamImpl : public ledger::LogStream {
 public:
  LogStreamImpl(const char* file,
                int line,
                const ledger::LogLevel log_level) {
    switch(log_level) {
      case ledger::LogLevel::LOG_INFO:
        log_message_ = std::make_unique<logging::LogMessage>(file, line, logging::LOG_INFO);
        break;
      case ledger::LogLevel::LOG_WARNING:
        log_message_ = std::make_unique<logging::LogMessage>(file, line, logging::LOG_WARNING);
        break;
      case ledger::LogLevel::LOG_ERROR:
        log_message_ = std::make_unique<logging::LogMessage>(file, line, logging::LOG_ERROR);
        break;
      default:
        log_message_ = std::make_unique<logging::LogMessage>(file, line, logging::LOG_VERBOSE);
        break;
    }
  }

  std::ostream& stream() override {
    return log_message_->stream();
  }

 private:
  std::unique_ptr<logging::LogMessage> log_message_;
  DISALLOW_COPY_AND_ASSIGN(LogStreamImpl);
};

} // anonymous namespace

BatLedgerClientMojoProxy::BatLedgerClientMojoProxy(
    mojom::BatLedgerClientAssociatedPtrInfo client_info) {
  bat_ledger_client_.Bind(std::move(client_info));
}

BatLedgerClientMojoProxy::~BatLedgerClientMojoProxy() {
}

std::string BatLedgerClientMojoProxy::GenerateGUID() const {
  if (!Connected())
    return "";

  std::string guid;
  bat_ledger_client_->GenerateGUID(&guid);
  return guid;
}

void OnLoadURL(const ledger::LoadURLCallback& callback,
    bool success, const std::string& response,
    const base::flat_map<std::string, std::string>& headers) {
  callback(success, response, mojo::FlatMapToMap(headers));
}

void BatLedgerClientMojoProxy::LoadURL(
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& content,
    const std::string& contentType,
    const ledger::URL_METHOD& method,
    ledger::LoadURLCallback callback) {
  if (!Connected())
    return;

  bat_ledger_client_->LoadURL(url, headers, content, contentType,
      ToMojomMethod(method), base::BindOnce(&OnLoadURL, std::move(callback)));
}

void BatLedgerClientMojoProxy::OnWalletInitialized(ledger::Result result) {
  if (!Connected())
    return;

  bat_ledger_client_->OnWalletInitialized(ToMojomResult(result));
}

void BatLedgerClientMojoProxy::OnWalletProperties(ledger::Result result,
    std::unique_ptr<ledger::WalletInfo> info) {
  if (!Connected())
    return;

  std::string json_info = info ? info->ToJson() : "";
  bat_ledger_client_->OnWalletProperties(ToMojomResult(result), json_info);
}

void BatLedgerClientMojoProxy::OnGrant(ledger::Result result,
    const ledger::Grant& grant) {
  if (!Connected())
    return;

  bat_ledger_client_->OnGrant(ToMojomResult(result), grant.ToJson());
}

void BatLedgerClientMojoProxy::OnGrantCaptcha(const std::string& image,
    const std::string& hint) {
  if (!Connected())
    return;

  bat_ledger_client_->OnGrantCaptcha(image, hint);
}

void BatLedgerClientMojoProxy::OnRecoverWallet(ledger::Result result,
    double balance, const std::vector<ledger::Grant>& grants) {
  if (!Connected())
    return;

  std::vector<std::string> grant_jsons;
  for (auto const& grant : grants) {
    grant_jsons.push_back(grant.ToJson());
  }

  bat_ledger_client_->OnRecoverWallet(
      ToMojomResult(result), balance, grant_jsons);
}

void BatLedgerClientMojoProxy::OnReconcileComplete(ledger::Result result,
    const std::string& viewing_id,
    ledger::PUBLISHER_CATEGORY category,
    const std::string& probi) {
  if (!Connected())
    return;

  bat_ledger_client_->OnReconcileComplete(ToMojomResult(result), viewing_id,
      ToMojomPublisherCategory(category), probi);
}

std::unique_ptr<ledger::LogStream> BatLedgerClientMojoProxy::Log(
    const char* file, int line, ledger::LogLevel level) const {
  // There's no need to proxy this
  return std::make_unique<LogStreamImpl>(file, line, level);
}

void BatLedgerClientMojoProxy::OnGrantFinish(ledger::Result result,
    const ledger::Grant& grant) {
  if (!Connected())
    return;

  bat_ledger_client_->OnGrantFinish(ToMojomResult(result), grant.ToJson());
}

void BatLedgerClientMojoProxy::OnLoadLedgerState(ledger::LedgerCallbackHandler* handler,
    int32_t result, const std::string& data) {
  handler->OnLedgerStateLoaded(ToLedgerResult(result), data);
}

void BatLedgerClientMojoProxy::LoadLedgerState(
    ledger::LedgerCallbackHandler* handler) {
  if (!Connected()) {
    handler->OnLedgerStateLoaded(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  bat_ledger_client_->LoadLedgerState(
      base::BindOnce(&BatLedgerClientMojoProxy::OnLoadLedgerState, AsWeakPtr(),
        base::Unretained(handler)));
}

void BatLedgerClientMojoProxy::OnLoadPublisherState(
    ledger::LedgerCallbackHandler* handler,
    int32_t result, const std::string& data) {
  handler->OnPublisherStateLoaded(ToLedgerResult(result), data);
}

void BatLedgerClientMojoProxy::LoadPublisherState(
    ledger::LedgerCallbackHandler* handler) {
  if (!Connected()) {
    handler->OnPublisherStateLoaded(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  bat_ledger_client_->LoadPublisherState(
      base::BindOnce(&BatLedgerClientMojoProxy::OnLoadPublisherState,
        AsWeakPtr(), base::Unretained(handler)));
}

void BatLedgerClientMojoProxy::OnLoadPublisherList(
    ledger::LedgerCallbackHandler* handler,
    int32_t result, const std::string& data) {
  handler->OnPublisherListLoaded(ToLedgerResult(result), data);
}

void BatLedgerClientMojoProxy::LoadPublisherList(
    ledger::LedgerCallbackHandler* handler) {
  if (!Connected()) {
    handler->OnPublisherListLoaded(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  bat_ledger_client_->LoadPublisherList(
      base::BindOnce(&BatLedgerClientMojoProxy::OnLoadPublisherList,
        AsWeakPtr(), base::Unretained(handler)));
}

void BatLedgerClientMojoProxy::OnSaveLedgerState(
    ledger::LedgerCallbackHandler* handler,
    int32_t result) {
  handler->OnLedgerStateSaved(ToLedgerResult(result));
}

void BatLedgerClientMojoProxy::SaveLedgerState(
    const std::string& ledger_state, ledger::LedgerCallbackHandler* handler) {
  if (!Connected()) {
    handler->OnLedgerStateSaved(ledger::Result::LEDGER_ERROR);
    return;
  }

  bat_ledger_client_->SaveLedgerState(ledger_state,
      base::BindOnce(&BatLedgerClientMojoProxy::OnSaveLedgerState,
        AsWeakPtr(), base::Unretained(handler)));
}

void BatLedgerClientMojoProxy::OnSavePublisherState(
    ledger::LedgerCallbackHandler* handler,
    int32_t result) {
  handler->OnPublisherStateSaved(ToLedgerResult(result));
}

void BatLedgerClientMojoProxy::SavePublisherState(
    const std::string& publisher_state,
    ledger::LedgerCallbackHandler* handler) {
  if (!Connected()) {
    handler->OnPublisherStateSaved(ledger::Result::LEDGER_ERROR);
    return;
  }

  bat_ledger_client_->SavePublisherState(publisher_state,
      base::BindOnce(&BatLedgerClientMojoProxy::OnSavePublisherState,
        AsWeakPtr(), base::Unretained(handler)));
}

void BatLedgerClientMojoProxy::OnSavePublishersList(
    ledger::LedgerCallbackHandler* handler,
    int32_t result) {
  handler->OnPublishersListSaved(ToLedgerResult(result));
}

void BatLedgerClientMojoProxy::SavePublishersList(
    const std::string& publishers_list,
    ledger::LedgerCallbackHandler* handler) {
  if (!Connected()) {
    handler->OnPublishersListSaved(ledger::Result::LEDGER_ERROR);
    return;
  }

  bat_ledger_client_->SavePublishersList(publishers_list,
      base::BindOnce(&BatLedgerClientMojoProxy::OnSavePublishersList,
        AsWeakPtr(), base::Unretained(handler)));
}

void OnSavePublisherInfo(const ledger::PublisherInfoCallback& callback,
    int32_t result, const std::string& publisher_info) {
  std::unique_ptr<ledger::PublisherInfo> info;
  if (!publisher_info.empty()) {
    info.reset(new ledger::PublisherInfo());
    info->loadFromJson(publisher_info);
  }
  callback(ToLedgerResult(result), std::move(info));
}

void BatLedgerClientMojoProxy::SavePublisherInfo(
    std::unique_ptr<ledger::PublisherInfo> publisher_info,
    ledger::PublisherInfoCallback callback) {
  if (!Connected()) {
    callback(ledger::Result::LEDGER_ERROR,
        std::unique_ptr<ledger::PublisherInfo>());
    return;
  }

  std::string json_info = publisher_info ? publisher_info->ToJson() : "";
  bat_ledger_client_->SavePublisherInfo(json_info,
      base::BindOnce(&OnSavePublisherInfo, std::move(callback)));
}

void OnLoadPublisherInfo(const ledger::PublisherInfoCallback& callback,
    int32_t result, const std::string& publisher_info) {
  std::unique_ptr<ledger::PublisherInfo> info;
  if (!publisher_info.empty()) {
    info.reset(new ledger::PublisherInfo());
    info->loadFromJson(publisher_info);
  }
  callback(ToLedgerResult(result), std::move(info));
}

void BatLedgerClientMojoProxy::LoadPublisherInfo(
    ledger::PublisherInfoFilter filter,
    ledger::PublisherInfoCallback callback) {
  if (!Connected()) {
    callback(ledger::Result::LEDGER_ERROR,
        std::unique_ptr<ledger::PublisherInfo>());
    return;
  }

  bat_ledger_client_->LoadPublisherInfo(filter.ToJson(),
      base::BindOnce(&OnLoadPublisherInfo, std::move(callback)));
}

void OnLoadPublisherInfoList(const ledger::PublisherInfoListCallback& callback,
    const std::vector<std::string>& publisher_info_list,
    uint32_t next_record) {
  ledger::PublisherInfoList list;

  for (const auto& publisher_info : publisher_info_list) {
    ledger::PublisherInfo info;
    info.loadFromJson(publisher_info);
    list.push_back(info);
  }

  callback(list, next_record);
}

void BatLedgerClientMojoProxy::LoadPublisherInfoList(
    uint32_t start,
    uint32_t limit,
    ledger::PublisherInfoFilter filter,
    ledger::PublisherInfoListCallback callback) {
  if (!Connected()) {
    callback(std::vector<ledger::PublisherInfo>(), 0);
    return;
  }

  bat_ledger_client_->LoadPublisherInfoList(start, limit, filter.ToJson(),
      base::BindOnce(&OnLoadPublisherInfoList, std::move(callback)));
}

void OnLoadMediaPublisherInfo(const ledger::PublisherInfoCallback& callback,
    int32_t result, const std::string& publisher_info) {
  std::unique_ptr<ledger::PublisherInfo> info;
  if (!publisher_info.empty()) {
    info.reset(new ledger::PublisherInfo());
    info->loadFromJson(publisher_info);
  }
  callback(ToLedgerResult(result), std::move(info));
}

void BatLedgerClientMojoProxy::LoadMediaPublisherInfo(
    const std::string& media_key,
    ledger::PublisherInfoCallback callback) {
  if (!Connected()) {
    callback(ledger::Result::LEDGER_ERROR,
        std::unique_ptr<ledger::PublisherInfo>());
    return;
  }

  bat_ledger_client_->LoadMediaPublisherInfo(media_key,
      base::BindOnce(&OnLoadMediaPublisherInfo, std::move(callback)));
}

void BatLedgerClientMojoProxy::SetTimer(uint64_t time_offset,
    uint32_t& timer_id) {
  if (!Connected()) {
    return;
  }

  bat_ledger_client_->SetTimer(time_offset, &timer_id); // sync
}

void BatLedgerClientMojoProxy::OnExcludedSitesChanged(
    const std::string& publisher_id) {
  if (!Connected()) {
    return;
  }

  bat_ledger_client_->OnExcludedSitesChanged(publisher_id);
}

void BatLedgerClientMojoProxy::OnPublisherActivity(ledger::Result result,
    std::unique_ptr<ledger::PublisherInfo> info,
    uint64_t windowId) {
  if (!Connected()) {
    return;
  }

  std::string json_info = info ? info->ToJson() : "";
  bat_ledger_client_->OnPublisherActivity(ToMojomResult(result),
      json_info, windowId);
}

void OnFetchFavIcon(const ledger::FetchIconCallback& callback,
    bool success, const std::string& favicon_url) {
  callback(success, favicon_url);
}

void BatLedgerClientMojoProxy::FetchFavIcon(const std::string& url,
    const std::string& favicon_key,
    ledger::FetchIconCallback callback) {
  if (!Connected()) {
    callback(false, "");
    return;
  }

  bat_ledger_client_->FetchFavIcon(url, favicon_key,
      base::BindOnce(&OnFetchFavIcon, std::move(callback)));
}

void OnGetRecurringDonations(const ledger::PublisherInfoListCallback& callback,
    const std::vector<std::string>& publisher_info_list,
    uint32_t next_record) {
  ledger::PublisherInfoList list;

  for (const auto& publisher_info : publisher_info_list) {
    ledger::PublisherInfo info;
    info.loadFromJson(publisher_info);
    list.push_back(info);
  }

  callback(list, next_record);
}

void BatLedgerClientMojoProxy::GetRecurringDonations(
    ledger::PublisherInfoListCallback callback) {
  if (!Connected()) {
    callback(std::vector<ledger::PublisherInfo>(), 0);
    return;
  }

  bat_ledger_client_->GetRecurringDonations(
      base::BindOnce(&OnGetRecurringDonations, std::move(callback)));
}

void OnLoadNicewareList(const ledger::GetNicewareListCallback& callback,
    int32_t result, const std::string& data) {
  callback(ToLedgerResult(result), data);
}

void BatLedgerClientMojoProxy::LoadNicewareList(
    ledger::GetNicewareListCallback callback) {
  if (!Connected()) {
    callback(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  bat_ledger_client_->LoadNicewareList(
      base::BindOnce(&OnLoadNicewareList, std::move(callback)));
}

void OnRecurringRemoved(const ledger::RecurringRemoveCallback& callback,
    int32_t result) {
  callback(ToLedgerResult(result));
}

void BatLedgerClientMojoProxy::OnRemoveRecurring(
    const std::string& publisher_key,
    ledger::RecurringRemoveCallback callback) {
  if (!Connected()) {
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }

  bat_ledger_client_->OnRemoveRecurring(publisher_key,
      base::BindOnce(&OnRecurringRemoved, std::move(callback)));
}

void BatLedgerClientMojoProxy::SaveContributionInfo(const std::string& probi,
    const int month,
    const int year,
    const uint32_t date,
    const std::string& publisher_key,
    const ledger::PUBLISHER_CATEGORY category) {
  if (!Connected())
    return;

  bat_ledger_client_->SaveContributionInfo(probi, month, year, date,
      publisher_key, ToMojomPublisherCategory(category));
}

void BatLedgerClientMojoProxy::SaveMediaPublisherInfo(
    const std::string& media_key, const std::string& publisher_id) {
  if (!Connected())
    return;

  bat_ledger_client_->SaveMediaPublisherInfo(media_key, publisher_id);
}

void BatLedgerClientMojoProxy::FetchWalletProperties() {
  if (!Connected())
    return;

  bat_ledger_client_->FetchWalletProperties();
}

void BatLedgerClientMojoProxy::FetchGrant(const std::string& lang,
    const std::string& paymentId) {
  if (!Connected())
    return;

  bat_ledger_client_->FetchGrant(lang, paymentId);
}

void BatLedgerClientMojoProxy::GetGrantCaptcha() {
  if (!Connected())
    return;

  bat_ledger_client_->GetGrantCaptcha();
}

std::string BatLedgerClientMojoProxy::URIEncode(const std::string& value) {
  if (!Connected())
    return "";

  std::string encoded_value;
  bat_ledger_client_->URIEncode(value, &encoded_value);
  return encoded_value;
}

void BatLedgerClientMojoProxy::SetContributionAutoInclude(
  const std::string& publisher_key, bool excluded, uint64_t windowId) {
  if (!Connected())
    return;

  bat_ledger_client_->SetContributionAutoInclude(
      publisher_key, excluded, windowId);
}

bool BatLedgerClientMojoProxy::Connected() const {
  return bat_ledger_client_.is_bound();
}

} // namespace bat_ledger
