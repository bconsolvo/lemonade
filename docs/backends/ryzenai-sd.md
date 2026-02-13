# RyzenAI SD Integration Guide

This document describes the integration of AMD Ryzen AI NPU Stable Diffusion support into Lemonade.

## Overview

The RyzenAI SD integration adds NPU-accelerated Stable Diffusion inference to Lemonade through a wrapped Python server architecture. The implementation consists of:

1. **Standalone Server**: [bconsolvo/ryzenai-sd-server](https://github.com/bconsolvo/ryzenai-sd-server)
2. **Lemonade Integration**: C++ backend + TypeScript frontend (this repository)

## Architecture

### Components

```
Lemonade App
    ├── C++ Server (lemonade)
    │   ├── RyzenAISDServer backend class
    │   │   ├── Process management
    │   │   ├── HTTP client
    │   │   └── Base64 encoding
    │   └── Recipe system integration
    │       ├── system_info.cpp (NPU detection)
    │       ├── recipe_options.cpp (parameter handling)
    │       └── router.cpp (backend routing)
    │
    ├── Python Server (ryzenai-sd-server)
    │   ├── Flask API (OpenAI compatible)
    │   ├── Inference engine (pipeline routing)
    │   └── ONNX Runtime + DirectML (NPU execution)
    │
    └── TypeScript Frontend
        ├── recipeOptionsConfig.ts (option definitions)
        ├── ChatWindow.tsx (UI controls)
        └── ModelManager.tsx (recipe display)
```

### Data Flow

1. **User Input** → ChatWindow.tsx collects prompt and settings
2. **Frontend** → Converts to API format, sends to C++ server
3. **C++ Backend** → RyzenAISDServer forwards HTTP request to Python server
4. **Python Server** → Routes to appropriate SD pipeline (SD1.5/SDXL/SD3)
5. **ONNX Runtime** → Executes on NPU via DirectML
6. **Response** → Base64 image returned through chain back to UI

## Implementation Details

### C++ Backend (`RyzenAISDServer`)

**Location**: `src/cpp/server/backends/ryzenai_sd_server.cpp`

**Key Methods**:
- `install(version)`: Downloads and extracts Python server release
- `load(model_path, params)`: Starts Python subprocess, waits for health check
- `unload()`: Terminates Python server process
- `image_generations(prompt, params)`: Sends HTTP POST to `/v1/images/generations`

**Lifecycle**:
```cpp
server->install("1.7.1");           // One-time setup
server->load("model-id", options);  // Start Python server
server->image_generations(...);      // Generate images
server->unload();                    // Clean up
```

### Recipe System Integration

**Files Modified**:
- `src/cpp/server/recipe_options.cpp`: Added 13 new options (negative_prompt, seed, sd3_mode, etc.)
- `src/cpp/server/system_info.cpp`: Registered ryzenai-sd recipe for XDNA2 NPU
- `src/cpp/server/router.cpp`: Added RyzenAISDServer backend routing
- `src/cpp/server/model_manager.cpp`: Added ryzenai-sd to download logic

**Recipe Registration**:
```cpp
{"ryzenai-sd", "default", {"windows"}, {{"npu", {"XDNA2"}}}}
```

### Frontend Integration

**TypeScript Files Modified**:
- `src/app/src/renderer/recipes/recipeOptionsConfig.ts`:
  - Added `RyzenAISDOptions` interface with 15+ parameters
  - Added 'ryzenai-sd' to `RecipeName` union type
  - Added option definitions for all SD3 modes
  - Added API field mappings (camelCase ↔ snake_case)

- `src/app/src/renderer/ChatWindow.tsx`:
  - Updated `isImageGenerationModel()` to include 'ryzenai-sd'
  - Added `isSD3Model()` helper for conditional UI logic
  - (Note: Full SD3 UI controls pending - shows TODO for future enhancement)

- `src/app/src/renderer/ModelManager.tsx`:
  - Added 'Ryzen AI SD NPU' display name for ryzenai-sd recipe

### Model Registry

**Location**: `src/cpp/resources/server_models.json`

**Added 7 Models**:
1. **SD-1.5-NPU** - `bconsolvo/stable-diffusion-1.5-amdnpu` (1.7 GB, 20 steps, 512x512)
2. **SD-Turbo-NPU** - `bconsolvo/sd-turbo-amdnpu` (1.7 GB, 1 step, 512x512)
3. **SDXL-Turbo-NPU** - `bconsolvo/sdxl-turbo-amdnpu` (2.5 GB, 4 steps, 512x512)
4. **SDXL-Base-NPU** - `bconsolvo/sdxl-base-amdnpu` (2.5 GB, 20 steps, 512x512)
5. **Segmind-Vega-NPU** - `bconsolvo/segmind-vega-amdnpu` (1.3 GB, 20 steps, 512x512)
6. **SD3-Medium-NPU** - `bconsolvo/stable-diffusion-3-medium-amdnpu` (4.7 GB, 28 steps, 512x512)
7. **SD3.5-Medium-NPU** - `bconsolvo/stable-diffusion-3.5-medium-amdnpu` (4.7 GB, 28 steps, 512x512)

**Model Capabilities**:
- **SD1.5, Turbo, SDXL, Vega**: Text-to-image, image-to-image
- **SD3, SD3.5**: All above + inpainting, removal, outpainting, ControlNet (canny/depth/tile)

**Default Settings** (mirroring sd-cpp):
- Resolution: 512x512 (all models)
- Steps: 20 (SD1.5/SDXL/Vega), 1 (Turbo), 4 (SDXL Turbo), 28 (SD3/3.5)
- CFG Scale: 7.0

## API Parameters

### Standard Parameters (All Models)

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `prompt` | string | *required* | Text description |
| `negative_prompt` | string | "" | Negative prompt |
| `width` | int | 512 | Image width |
| `height` | int | 512 | Image height |
| `steps` | int | 20 | Inference steps |
| `cfg_scale` | float | 7.0 | Guidance scale |
| `seed` | int | -1 | Random seed |
| `num_images` | int | 1 | Batch size |

### Image-to-Image (All Models)

| Parameter | Type | Description |
|-----------|------|-------------|
| `init_image_path` | string | Path to initial image |
| `strength` | float | Denoising strength (0.0-1.0) |

### SD3-Specific Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `sd3_mode` | string | Mode: text2img, img2img, inpainting, removal, outpainting, controlnet_* |
| `controlnet_conditioning_scale` | float | ControlNet strength (0.0-2.0) |
| `t5_sequence_len` | int | T5 encoder length (128-512) |
| `control_image_path` | string | Control image for ControlNet |
| `control_mask_path` | string | Mask for inpainting/removal |
| `image_pads` | string | Outpainting pads: "left,right,top,bottom" |

## Usage Example

### Command Line

```bash
# Start Lemonade server
lemonade serve

# In another terminal, load model
lemonade load SD-1.5-NPU

# Generate image
lemonade chat --model SD-1.5-NPU --prompt "A serene mountain landscape at sunset"
```

### Python API

```python
from lemonade import Lemonade

client = Lemonade()

# Load NPU model
client.load_model("SD-1.5-NPU")

# Text-to-image
response = client.generate_image(
    model="SD-1.5-NPU",
    prompt="A futuristic cityscape",
    negative_prompt="blurry, low quality",
    width=512,
    height=512,
    steps=20,
    cfg_scale=7.0,
    seed=42
)

# Image-to-image
response = client.generate_image(
    model="SD-1.5-NPU",
    prompt="Make it a watercolor painting",
    init_image="input.png",
    strength=0.5
)

# SD3 ControlNet
response = client.generate_image(
    model="SD3-Medium-NPU",
    prompt="Architectural drawing",
    sd3_mode="controlnet_canny",
    control_image="edges.png",
    controlnet_conditioning_scale=0.7,
    t5_sequence_len=256,
    steps=28
)
```

## Building

### Prerequisites

- CMake 3.20+
- Visual Studio 2019+ (Windows)
- Node.js 18+ (for frontend)
- Python 3.10-3.12
- AMD RyzenAI SDK 1.7.1+

### Build Steps

```powershell
# Clone repository
git clone https://github.com/bconsolvo/lemonade.git
cd lemonade

# Build C++ server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Build frontend
cd src/app
npm install
npm run build

# Package
cd ../..
cpack -C Release
```

## Testing

### Verify Installation

```powershell
# Check if NPU is detected
lemonade --system-info

# Expected output should include:
# NPU: AMD XDNA2 (Ryzen AI 300/400)
# Recipes: [..., ryzenai-sd, ...]
```

### Test Model Loading

```powershell
# Load a small model first
lemonade load SD-Turbo-NPU

# Check status
lemonade status
```

### Test Image Generation

```powershell
# Quick test with SD Turbo (1 step)
lemonade chat --model SD-Turbo-NPU --prompt "A red car" --steps 1

# Full test with SD 1.5
lemonade chat --model SD-1.5-NPU --prompt "Beautiful sunset" --steps 20 --cfg-scale 7.0
```

## Troubleshooting

### NPU Not Detected

**Symptom**: ryzenai-sd recipe not available

**Solutions**:
1. Verify Ryzen AI 300/400 series processor
2. Check Device Manager → Neural Processors → AMD IPU Device
3. Update AMD drivers
4. Install RyzenAI SDK 1.7.1+

### Python Server Fails to Start

**Symptom**: "Server failed to start within timeout"

**Solutions**:
1. Check Python installation: `python --version` (3.10-3.12)
2. Install dependencies: `pip install -r requirements.txt`
3. Verify onnx_custom_ops.dll: Check `C:\Program Files\RyzenAI\1.7.1\deployment\`
4. Check server logs in `~/.lemonade/logs/ryzenai-sd-server.log`

### Model Download Issues

**Symptom**: "Failed to download model"

**Solutions**:
1. Check internet connectivity
2. Verify HuggingFace access (no login required for public models)
3. Check disk space (models range from 1.3-4.7 GB)
4. Try manual download: `lemonade pull bconsolvo/stable-diffusion-1.5-amdnpu`

### Slow Inference

**Symptom**: Generation takes > 30 seconds

**Solutions**:
1. Verify NPU is being used (check Task Manager → NPU usage)
2. Close other NPU-intensive applications
3. Try smaller model (SD-Turbo-NPU for fastest)
4. Check NPU mode in Power Options (should be "Best Performance")

### Out of Memory

**Symptom**: "RuntimeError: Out of memory"

**Solutions**:
1. Reduce image size: use 512x512 instead of 1024x1024
2. Lower num_images to 1
3. Close other applications
4. For SDXL/SD3: Requires 16GB+ RAM

## Performance Benchmarks

### AMD Ryzen AI 9 HX 370 (XDNA2 NPU)

| Model | Resolution | Steps | Time | Images/sec |
|-------|------------|-------|------|------------|
| SD-Turbo-NPU | 512x512 | 1 | ~2s | 0.5 |
| SD-1.5-NPU | 512x512 | 20 | ~15s | 0.067 |
| SDXL-Turbo-NPU | 512x512 | 4 | ~8s | 0.125 |
| SDXL-Base-NPU | 512x512 | 20 | ~30s | 0.033 |
| SD3-Medium-NPU | 512x512 | 28 | ~40s | 0.025 |

*Note: Benchmarks exclude model loading time (~5-10s)*

## Future Enhancements

### Planned Features

1. **Enhanced UI Controls** (ChatWindow.tsx):
   - SD3 mode selector dropdown
   - File upload buttons for init_image, control_image, control_mask
   - Conditional disabling of SD3-only options for non-SD3 models
   - Image pads visual editor for outpainting
   - ControlNet type selector with preview

2. **Advanced Pipeline Features**:
   - Multi-image batch generation
   - Progress callbacks during generation
   - Intermediate step visualization
   - LoRA model support
   - Custom VAE selection

3. **Model Management**:
   - One-click model updates
   - Model variant selection (fp16, int8)
   - Automatic model pruning/cleanup
   - Model performance analytics

4. **Integration Enhancements**:
   - Gallery view for generated images
   - Prompt history and favorites
   - Style presets
   - Negative prompt templates

### Contributing

See the main [Lemonade contributing guide](../../CONTRIBUTING.md) for details on:
- Code style guidelines
- Pull request process
- Testing requirements
- Documentation standards

## License

This integration is licensed under the MIT License - see [LICENSE](../../LICENSE).

AMD RyzenAI SDK components (DLLs) are subject to the AMD Software License Agreement - see [AMD_LICENSE](https://github.com/bconsolvo/ryzenai-sd-server/blob/main/AMD_LICENSE).

## Support

- **Lemonade Issues**: [GitHub Issues](https://github.com/bconsolvo/lemonade/issues)
- **RyzenAI SD Server**: [GitHub Issues](https://github.com/bconsolvo/ryzenai-sd-server/issues)
- **AMD Community**: [AMD Forums](https://community.amd.com/)

## Related Documentation

- [RyzenAI SD Server Full Documentation](https://github.com/bconsolvo/ryzenai-sd-server)
- [Lemonade Backend Development Guide](../backends/README.md)
- [Recipe System Documentation](../recipes/README.md)
- [Model Registry Guide](../models/README.md)
