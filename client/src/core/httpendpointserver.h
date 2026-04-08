#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <functional>
#include <mutex>

/**
 * HTTP 端点服务器（《客户端产品化》§4 运维可观测性）
 *
 * 提供以下 HTTP 端点：
 * - GET /metrics      - Prometheus 格式指标
 * - GET /metrics/json - JSON 格式指标
 * - GET /health       - 健康状态 JSON
 * - GET /ready       - K8s 就绪状态
 *
 * 特性：
 * - 简单实现：使用 QTcpServer + 手写 HTTP 解析
 * - 线程安全：使用 mutex 保护指标数据
 * - 可扩展：支持注册自定义处理函数
 *
 * 使用：
 *   HttpEndpointServer::instance().start(9080);
 *   HttpEndpointServer::instance().registerHandler("/custom", [](){ return "custom response"; });
 */
class HttpEndpointServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int port READ port NOTIFY serverStarted)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    static HttpEndpointServer& instance();

    // 禁用拷贝
    HttpEndpointServer(const HttpEndpointServer&) = delete;
    HttpEndpointServer& operator=(const HttpEndpointServer&) = delete;

    // ═══════════════════════════════════════════════════════════════════════════
    // 生命周期
    // ═══════════════════════════════════════════════════════════════════════════

    // 启动 HTTP 服务器
    bool start(int port = 9080);

    // 停止服务器
    void stop();

    // 服务器是否运行中
    bool isRunning() const { return m_running; }
    int port() const { return m_port; }

    // ═══════════════════════════════════════════════════════════════════════════
    // 端点注册
    // ═══════════════════════════════════════════════════════════════════════════

    // 注册自定义处理函数
    using Handler = std::function<QString()>;
    void registerHandler(const QString& path, Handler handler);

    // 注册 JSON 处理函数
    using JsonHandler = std::function<QJsonObject()>;
    void registerJsonHandler(const QString& path, JsonHandler handler);

    // 取消注册
    void unregisterHandler(const QString& path);

    // ═══════════════════════════════════════════════════════════════════════════
    // 内置端点数据
    // ═══════════════════════════════════════════════════════════════════════════

    // 更新指标值
    void setMetric(const QString& name, double value);
    void setMetric(const QString& name, const QString& value);
    void incrementMetric(const QString& name, double delta = 1.0);

    // 更新健康状态
    void setHealthStatus(const QString& component, bool healthy, const QString& message = "");

    // 更新就绪状态
    void setReadyStatus(bool ready, const QString& reason = "");

signals:
    // 服务器启动信号
    void serverStarted(int port);
    void serverError(const QString& error);
    void runningChanged(bool running);

    // 请求信号（用于监控/调试）
    void requestReceived(const QString& method, const QString& path, int statusCode);

private:
    explicit HttpEndpointServer(QObject* parent = nullptr);
    ~HttpEndpointServer() override;

    // HTTP 请求处理
    void handleRequest(QTcpSocket* socket);
    QString parseHttpMethod(const QString& line);
    QString parseHttpPath(const QString& line);
    QString buildHttpResponse(int statusCode, const QString& contentType, const QString& body);
    QString urlDecode(const QString& encoded);

    // 内置端点处理
    QString handleMetrics();
    QString handleMetricsJson();
    QString handleHealth();
    QString handleReady();

    // Prometheus 格式辅助
    QString escapePrometheusLabel(const QString& value);
    double getPrometheusTimestamp();

    // 数据成员
    QTcpServer* m_server = nullptr;
    int m_port = 9080;
    bool m_running = false;

    // 线程安全的指标存储
    std::mutex m_metricsMutex;
    QMap<QString, double> m_numericMetrics;
    QMap<QString, QString> m_stringMetrics;

    // 线程安全的健康状态
    std::mutex m_healthMutex;
    QMap<QString, QPair<bool, QString>> m_healthStatus;  // component -> (healthy, message)

    // 就绪状态
    std::mutex m_readyMutex;
    bool m_ready = false;
    QString m_readyReason;

    // 自定义处理器
    std::mutex m_handlersMutex;
    QMap<QString, Handler> m_handlers;
    QMap<QString, JsonHandler> m_jsonHandlers;

    // 请求计数
    std::atomic<quint64> m_requestCount{0};
    std::atomic<quint64> m_errorCount{0};
};
