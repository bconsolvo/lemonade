#pragma once

#include "lemon/wrapped_server.h"
#include "lemon/model_manager.h"
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace lemon {
namespace backends {

/**
 * RyzenAI SD Server - Wrapper for Python-based RyzenAI Stable Diffusion server
 * 
 * Manages a Python subprocess running the ryzenai-sd-server Flask application
 * for NPU-accelerated Stable Diffusion inference.
 */
class RyzenAISDServer : public WrappedServer {
public:
    RyzenAISDServer(const std::string& log_level, ModelManager* model_manager);
    ~RyzenAISDServer() override;

    // WrappedServer interface
    json install(const std::string& version) override;
    json load(const std::string& model_path, const json& params) override;
    json unload() override;

    // Image generation interface
    json image_generations(const std::string& prompt, const json& params);

protected:
    std::string get_server_type() const override { return "ryzenai-sd"; }

private:
    std::string version_;
    std::string server_path_;
    int server_port_ = 8282;
    
    json send_http_request(const std::string& endpoint, const std::string& method, const json& body = {});
    std::string read_file_to_base64(const std::string& file_path);
};

} // namespace backends
} // namespace lemon
