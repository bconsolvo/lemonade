#include "lemon/backends/ryzenai_sd_server.h"
#include "lemon/error_types.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cpr/cpr.h>

namespace lemon {
namespace backends {

RyzenAISDServer::RyzenAISDServer(const std::string& log_level, ModelManager* model_manager)
    : WrappedServer(log_level, model_manager) {
    std::cout << "[RyzenAISDServer] Initialized" << std::endl;
}

RyzenAISDServer::~RyzenAISDServer() {
    try {
        unload();
    } catch (...) {
        // Suppress exceptions in destructor
    }
}

json RyzenAISDServer::install(const std::string& version) {
    std::cout << "[RyzenAISDServer] Installing version: " << version << std::endl;
    version_ = version;
    
    // Determine server installation path
    auto lemonade_home = std::filesystem::path(std::getenv("LEMONADE_HOME") ? std::getenv("LEMONADE_HOME") : ".");
    server_path_ = (lemonade_home / "servers" / "ryzenai-sd-server" / version).string();
    
    std::cout << "[RyzenAISDServer] Server path: " << server_path_ << std::endl;
    
    // Check if already installed
    if (std::filesystem::exists(server_path_)) {
        std::cout << "[RyzenAISDServer] Already installed" << std::endl;
        return {{"status", "success"}, {"message", "Already installed"}};
    }
    
    // Download and extract release
    std::string download_url = "https://github.com/bconsolvo/ryzenai-sd-server/releases/download/v" + 
                               version + "/ryzenai-sd-server-v" + version + ".zip";
    
    std::cout << "[RyzenAISDServer] Downloading from: " << download_url << std::endl;
    
    // TODO: Implement actual download and extraction using model_manager's download utilities
    // For now, return success assuming manual installation
    
    return {{"status", "success"}, {"message", "Installation initiated"}};
}

json RyzenAISDServer::load(const std::string& model_path, const json& params) {
    std::cout << "[RyzenAISDServer] Loading model: " << model_path << std::endl;
    
    // Check server installation
    if (!std::filesystem::exists(server_path_)) {
        throw LemonError("RyzenAI SD Server not installed. Run install() first.", "INSTALL_REQUIRED");
    }
    
    // Build Python command to start server
    std::string python_cmd = "python";
    std::string main_script = (std::filesystem::path(server_path_) / "src" / "main.py").string();
    
    std::vector<std::string> args = {
        python_cmd,
        "-m", "src.main",
        "--model-id", model_path,
        "--host", "127.0.0.1",
        "--port", std::to_string(server_port_)
    };
    
    // Start server process
    std::string cmd = python_cmd + " -m src.main --model-id \"" + model_path + 
                     "\" --host 127.0.0.1 --port " + std::to_string(server_port_);
    
    std::cout << "[RyzenAISDServer] Starting server: " << cmd << std::endl;
    
    // Use WrappedServer's process management
    start_process(cmd, server_path_);
    
    // Wait for server to be ready
    int max_attempts = 30;
    for (int i = 0; i < max_attempts; i++) {
        try {
            auto response = send_http_request("/health", "GET");
            if (response["status"] == "healthy") {
                std::cout << "[RyzenAISDServer] Server ready" << std::endl;
                return {{"status", "success"}, {"message", "Model loaded"}};
            }
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    throw LemonError("Server failed to start within timeout", "TIMEOUT");
}

json RyzenAISDServer::unload() {
    std::cout << "[RyzenAISDServer] Unloading model" << std::endl;
    stop_process();
    return {{"status", "success"}, {"message", "Model unloaded"}};
}

json RyzenAISDServer::image_generations(const std::string& prompt, const json& params) {
    std::cout << "[RyzenAISDServer] Generating images for prompt: " << prompt.substr(0, 50) << "..." << std::endl;
    
    // Build request body
    json request_body = {
        {"prompt", prompt}
    };
    
    // Standard parameters
    if (params.contains("negative_prompt")) request_body["negative_prompt"] = params["negative_prompt"];
    if (params.contains("width")) request_body["width"] = params["width"];
    if (params.contains("height")) request_body["height"] = params["height"];
    if (params.contains("steps")) request_body["steps"] = params["steps"];
    if (params.contains("cfg_scale")) request_body["cfg_scale"] = params["cfg_scale"];
    if (params.contains("seed")) request_body["seed"] = params["seed"];
    if (params.contains("num_images")) request_body["num_images"] = params["num_images"];
    
    // Image-to-image parameters
    if (params.contains("init_image_path") && !params["init_image_path"].get<std::string>().empty()) {
        std::string init_image_b64 = read_file_to_base64(params["init_image_path"]);
        request_body["init_image"] = init_image_b64;
        if (params.contains("strength")) request_body["strength"] = params["strength"];
    }
    
    // SD3-specific parameters
    if (params.contains("sd3_mode")) request_body["sd3_mode"] = params["sd3_mode"];
    if (params.contains("controlnet_conditioning_scale")) {
        request_body["controlnet_conditioning_scale"] = params["controlnet_conditioning_scale"];
    }
    if (params.contains("t5_sequence_len")) request_body["t5_sequence_len"] = params["t5_sequence_len"];
    
    // Control images
    if (params.contains("control_image_path") && !params["control_image_path"].get<std::string>().empty()) {
        std::string control_image_b64 = read_file_to_base64(params["control_image_path"]);
        request_body["control_image"] = control_image_b64;
    }
    if (params.contains("control_mask_path") && !params["control_mask_path"].get<std::string>().empty()) {
        std::string control_mask_b64 = read_file_to_base64(params["control_mask_path"]);
        request_body["control_mask"] = control_mask_b64;
    }
    
    // Image pads for outpainting
    if (params.contains("image_pads") && !params["image_pads"].get<std::string>().empty()) {
        // Parse comma-separated values: "left,right,top,bottom"
        std::string pads_str = params["image_pads"];
        std::vector<int> pads;
        std::stringstream ss(pads_str);
        std::string item;
        while (std::getline(ss, item, ',')) {
            pads.push_back(std::stoi(item));
        }
        if (pads.size() == 4) {
            request_body["image_pads"] = pads;
        }
    }
    
    // Send request to server
    return send_http_request("/v1/images/generations", "POST", request_body);
}

json RyzenAISDServer::send_http_request(const std::string& endpoint, const std::string& method, const json& body) {
    std::string url = "http://127.0.0.1:" + std::to_string(server_port_) + endpoint;
    
    cpr::Response response;
    if (method == "GET") {
        response = cpr::Get(cpr::Url{url});
    } else if (method == "POST") {
        response = cpr::Post(
            cpr::Url{url},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{body.dump()}
        );
    }
    
    if (response.status_code != 200) {
        throw LemonError("HTTP request failed: " + std::to_string(response.status_code) + " " + response.text,
                        "HTTP_ERROR");
    }
    
    return json::parse(response.text);
}

std::string RyzenAISDServer::read_file_to_base64(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw LemonError("Failed to open file: " + file_path, "FILE_ERROR");
    }
    
    std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    
    // Base64 encode (using a simple implementation or library)
    // For this example, we'll use a placeholder - in practice, use a proper base64 library
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string encoded;
    int val = 0;
    int valb = -6;
    
    for (unsigned char c : buffer) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(base64_chars[((val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) {
        encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    
    while (encoded.size() % 4) {
        encoded.push_back('=');
    }
    
    return encoded;
}

} // namespace backends
} // namespace lemon
