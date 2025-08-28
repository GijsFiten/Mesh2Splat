///////////////////////////////////////////////////////////////////////////////
//         Mesh2Splat: fast mesh to 3D gaussian splat conversion             //
//        Copyright (c) 2025 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#include <string>
#include <filesystem>
#include <iostream>
#include "utils/normalizedUvUnwrapping.hpp"
#include "renderer/renderer.hpp"
#include "glewGlfwHandlers/glewGlfwHandler.hpp"
#include "renderer/guiRendererConcreteMediator.hpp"
#include "renderer/IoHandler.hpp"
#include "utils/Camera.hpp"
#include "imGuiUi/ImGuiUi.hpp"

// Forward declaration for CLI mode
int runCLIMode(int argc, char** argv);

int main(int argc, char** argv) {
    // Check for CLI mode - if arguments provided, run in CLI mode
    if (argc >= 2) {
        return runCLIMode(argc, argv);
    }
    
    GlewGlfwHandler glewGlfwHandler(glm::ivec2(1080, 720), "Mesh2Splat");
    
    Camera camera(
        glm::vec3(0.0f, 0.0f, 5.0f), 
        glm::vec3(0.0f, 1.0f, 0.0f), 
        -90.0f, 
        0.0f
    );  

    IoHandler ioHandler(glewGlfwHandler.getWindow(), camera);
    if(glewGlfwHandler.init() == -1) return -1;

    ioHandler.setupCallbacks();

    ImGuiUI ImGuiUI(0.65f, 0.5f); //TODO: give a meaning to these params
    ImGuiUI.initialize(glewGlfwHandler.getWindow());

    Renderer renderer(glewGlfwHandler.getWindow(), camera);
    renderer.initialize();
    GuiRendererConcreteMediator guiRendererMediator(renderer, ImGuiUI);

    float deltaTime = 0.0f; 
    float lastFrame = 0.0f;

    while (!glfwWindowShouldClose(glewGlfwHandler.getWindow())) {
        
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();
        
        ioHandler.processInput(deltaTime);

        renderer.clearingPrePass(ImGuiUI.getSceneBackgroundColor());

        ImGuiUI.preframe();
        ImGuiUI.renderUI();
        
        guiRendererMediator.update();

        renderer.renderFrame();

        ImGuiUI.displayGaussianCounts(renderer.getTotalGaussianCount(), renderer.getVisibleGaussianCount());
        ImGuiUI.postframe();

        glfwSwapBuffers(glewGlfwHandler.getWindow());
    }

    glfwTerminate();

    return 0;
}

// CLI Mode Implementation
int runCLIMode(int argc, char** argv) {
    // Check for help argument
    if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" || std::string(argv[1]) == "/?" || std::string(argv[1]) == "help")) {
        std::cout << "Usage: mesh2splat <input.glb> [output.ply] [resolution]" << std::endl;
        std::cout << "  input.glb    - Input GLB/GLTF mesh file" << std::endl;
        std::cout << "  output.ply   - Output PLY file (optional, defaults to input name with .ply extension)" << std::endl;
        std::cout << "  resolution   - Conversion resolution (optional, defaults to 512)" << std::endl;
        return 0;
    }
    
    if (argc < 2) {
        std::cout << "Usage: mesh2splat <input.glb> [output.ply] [resolution]" << std::endl;
        std::cout << "  input.glb    - Input GLB/GLTF mesh file" << std::endl;
        std::cout << "  output.ply   - Output PLY file (optional, defaults to input name with .ply extension)" << std::endl;
        std::cout << "  resolution   - Conversion resolution (optional, defaults to 512)" << std::endl;
        return -1;
    }
    
    std::string inputFile = argv[1];
    std::filesystem::path outputFile;
    unsigned int resolutionTarget = 512; // Default resolution that works well
    
    if (argc >= 3) {
        outputFile = argv[2];
    } else {
        // Generate output filename: same directory, same name, .ply extension
        std::filesystem::path inputPath(inputFile);
        outputFile = inputPath.parent_path() / (inputPath.stem().string() + ".ply");
    }
    
    if (argc >= 4) {
        try {
            resolutionTarget = std::stoi(argv[3]);
            if (resolutionTarget < 64 || resolutionTarget > 2048) {
                std::cerr << "Warning: Resolution should be between 64 and 2048. Using default 512." << std::endl;
                resolutionTarget = 512;
            }
        } catch (...) {
            std::cerr << "Warning: Invalid resolution specified. Using default 512." << std::endl;
            resolutionTarget = 512;
        }
    }
    
    std::cout << "CLI Mode: Converting " << inputFile << " to " << outputFile.string() << " (resolution: " << resolutionTarget << ")" << std::endl;
    
    // Check if input file exists
    if (!std::filesystem::exists(inputFile)) {
        std::cerr << "Error: Input file does not exist: " << inputFile << std::endl;
        return -1;
    }
    
    // Create headless OpenGL context (invisible window for CLI)
    GlewGlfwHandler glewGlfwHandler(glm::ivec2(1024, 1024), "Mesh2Splat-CLI", false);
    if(glewGlfwHandler.init() == -1) {
        std::cerr << "Error: Failed to initialize OpenGL context" << std::endl;
        return -1;
    }
    
    // Create minimal camera (not used for conversion but needed for renderer)
    Camera camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
    
    // Initialize renderer
    Renderer renderer(glewGlfwHandler.getWindow(), camera);
    renderer.initialize();
    
    // Get parent folder for mesh loading
    std::filesystem::path inputPath(inputFile);
    std::string parentFolder = inputPath.parent_path().string();
    std::string meshFilePath = inputFile;
    
    try {
        // Step 1: Load the mesh
        std::cout << "Loading mesh..." << std::endl;
        renderer.resetModelMatrices();
        renderer.getSceneManager().loadModel(meshFilePath, parentFolder);
        
        // Set up conversion parameters
        renderer.gaussianBufferFromSize(resolutionTarget * resolutionTarget);
        renderer.setFormatType(0); // Default format
        renderer.setViewportResolutionForConversion(resolutionTarget);
        
        // Set parameters that are normally provided by the GUI
        renderer.setStdDevFromImGui(1.0f); // Default gaussian standard deviation
        renderer.setRenderMode(ImGuiUI::VisualizationOption::ALBEDO); // Default render mode
        
        // Enable required render passes (as per LoadModel event)
        renderer.enableRenderPass("conversion");
        renderer.enableRenderPass("gaussianPrepass");
        renderer.enableRenderPass("radixSort");
        renderer.enableRenderPass("gaussianSplatting");
        
        std::cout << "Mesh loaded successfully." << std::endl;
        
        // Step 2: Run conversion (as per RunConversion event)
        std::cout << "Converting to 3D Gaussian Splats..." << std::endl;
        
        // Focus on conversion pass
        renderer.enableRenderPass("conversion");
        renderer.setViewportResolutionForConversion(resolutionTarget);
        
        // Execute one render frame to perform the conversion
        renderer.renderFrame();
        
        // Re-enable gaussian rendering passes after conversion
        renderer.enableRenderPass("gaussianPrepass");
        renderer.enableRenderPass("radixSort");
        renderer.enableRenderPass("gaussianSplatting");
        
        // Reset viewport after conversion (as per EnableGaussianRendering event)
        renderer.resetRendererViewportResolution();
        
        std::cout << "Conversion completed. Generated " << renderer.getTotalGaussianCount() << " gaussians." << std::endl;
        
        // Step 3: Export to PLY
        std::cout << "Exporting to PLY format..." << std::endl;
        renderer.getSceneManager().exportPlySynchronous(outputFile.string(), 0); // Default format
        
        std::cout << "Successfully exported to: " << outputFile.string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error during conversion: " << e.what() << std::endl;
        glfwTerminate();
        return -1;
    }
    
    glfwTerminate();
    return 0;
}

