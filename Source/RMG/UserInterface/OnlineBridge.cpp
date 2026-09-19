#include "OnlineBridge.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>

namespace UserInterface
{

// Safe to embed: this is the Supabase "publishable" (anon) key, the same one
// used by the public website. It only grants what Row Level Security allows
// an anonymous visitor — never the secret/service_role key.
static const char* const SUPABASE_URL = "https://jexxpiumbulpuashtjia.supabase.co";
static const char* const SUPABASE_ANON_KEY = "sb_publishable_AS3AbHJZ-nMJsdRkZbXFAA_M6aeTWVB";
static const char* const SHARED_MEM_NAME = "Local\\SmashRemixOnlineBridge";

OnlineBridge::OnlineBridge(QObject* parent) : QObject(parent)
{
    if (!openSharedMemory())
    {
        return;
    }

    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::finished, this, &OnlineBridge::onResolveCodeReply);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &OnlineBridge::pollSharedMemory);
    m_pollTimer->start(250);
}

OnlineBridge::~OnlineBridge()
{
    if (m_shared != nullptr)
    {
        UnmapViewOfFile(m_shared);
    }
    if (m_mapping != nullptr)
    {
        CloseHandle(m_mapping);
    }
}

bool OnlineBridge::openSharedMemory()
{
    const DWORD size = sizeof(OnlineBridgeShared);

    m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, SHARED_MEM_NAME);
    if (m_mapping == nullptr)
    {
        return false;
    }

    const bool alreadyExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

    m_shared = static_cast<OnlineBridgeShared*>(MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, size));
    if (m_shared == nullptr)
    {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
        return false;
    }

    if (!alreadyExisted)
    {
        memset(m_shared, 0, size);
        memcpy(m_shared->signature, "SRONLINEV1", 11);
    }

    return true;
}

void OnlineBridge::pollSharedMemory()
{
    if (m_shared == nullptr || m_requestInFlight)
    {
        return;
    }

    if (memcmp(m_shared->signature, "SRONLINEV1", 10) != 0)
    {
        return;
    }

    const uint32_t requestId = m_shared->requestId;
    if (requestId == m_lastSeenRequestId)
    {
        return; // nothing new
    }
    m_lastSeenRequestId = requestId;

    const uint32_t command = m_shared->command;
    if (command == ONLINE_BRIDGE_CMD_RESOLVE_CODE)
    {
        char rawCode[9] = {0};
        memcpy(rawCode, m_shared->inputCode, 8);
        const QString code = QString::fromLatin1(rawCode).trimmed();

        if (m_shared != nullptr)
        {
            m_shared->status = ONLINE_BRIDGE_STATUS_WORKING; // intermediate: responseId stays behind on purpose
        }

        if (code.isEmpty())
        {
            writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
            return;
        }

        QNetworkRequest req(QUrl(QString("%1/rest/v1/rpc/resolve_player_code").arg(SUPABASE_URL)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("apikey", SUPABASE_ANON_KEY);
        req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
        req.setAttribute(QNetworkRequest::User, requestId);

        QJsonObject body;
        body["p_code"] = code;

        m_requestInFlight = true;
        m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
}

void OnlineBridge::onResolveCodeReply(QNetworkReply* reply)
{
    reply->deleteLater();
    m_requestInFlight = false;

    const uint32_t requestId = reply->request().attribute(QNetworkRequest::User).toUInt();

    if (reply->error() != QNetworkReply::NoError)
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray() || doc.array().isEmpty())
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_NOT_FOUND);
        return;
    }

    const QJsonObject row = doc.array().first().toObject();
    const QString nickname = row["nickname"].toString();
    writeStatus(requestId, ONLINE_BRIDGE_STATUS_FOUND, nickname);
}

void OnlineBridge::writeStatus(uint32_t requestId, uint32_t status, const QString& nickname)
{
    if (m_shared == nullptr)
    {
        return;
    }

    m_shared->status = status;
    if (!nickname.isEmpty())
    {
        const QByteArray utf8 = nickname.toUtf8().left(23);
        memset(m_shared->resultNickname, 0, sizeof(m_shared->resultNickname));
        memcpy(m_shared->resultNickname, utf8.constData(), utf8.size());
    }
    m_shared->responseId = requestId;
}

} // namespace UserInterface
