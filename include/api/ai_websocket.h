#pragma once

#include <atomic>
#include <drogon/HttpRequest.h>
#include <drogon/WebSocketController.h>
#include <memory>
#include <string>

using namespace drogon;

// Forward declarations
class IInstanceManager;

/**
 * @brief WebSocket controller for real-time AI streaming
 *
 * Endpoint: /v1/securt/ai/stream
 * Supports bidirectional communication for streaming AI results
 */
class AIWebSocketController
    : public drogon::WebSocketController<AIWebSocketController> {
public:
  void handleNewMessage(const WebSocketConnectionPtr &wsConnPtr,
                        std::string &&message,
                        const WebSocketMessageType &type) override;

  void handleNewConnection(const HttpRequestPtr &req,
                           const WebSocketConnectionPtr &wsConnPtr) override;

  void handleConnectionClosed(const WebSocketConnectionPtr &wsConnPtr) override;

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/v1/securt/ai/stream", drogon::Get);
  WS_PATH_ADD("/v1/securt/instance/{instanceId}/stream", drogon::Get);
  WS_PATH_LIST_END

  /**
   * @brief Set instance manager (dependency injection)
   */
  static void setInstanceManager(IInstanceManager *manager);

private:
  void processStreamMessage(const WebSocketConnectionPtr &wsConnPtr,
                            const std::string &message);

  void processInstanceStreamMessage(const WebSocketConnectionPtr &wsConnPtr,
                                    const std::string &message,
                                    const std::string &instanceId);

  void sendResult(const WebSocketConnectionPtr &wsConnPtr,
                  const std::string &result);

  void sendInstanceUpdate(const WebSocketConnectionPtr &wsConnPtr,
                          const std::string &instanceId);

  static std::atomic<size_t> active_connections_;
  static IInstanceManager *instance_manager_;
};
