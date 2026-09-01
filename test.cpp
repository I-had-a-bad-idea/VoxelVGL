
// Includes from the VulkanGraphicsLib
#include <VGL/renderer.h>
#include <VGL/object.h>

#include <iostream>


int main() {
    std::cout << "Starting...\n";
    Renderer renderer("Test", 1280, 720, true, nullptr, false, UINT32_MAX); // Create a renderer with window dimensions + capture mouse
    Scene scene; // A scene is used to group objects
    
    // Load meshes, textures and shaders
    std::cout << "Loading resources...\n";
    Mesh monkey_mesh = renderer.load_mesh("assets/monkey.obj"); // Currently only .obj is supported
    Texture gravel_texture = renderer.load_texture("assets/Textures/Gravel.ktx"); // Currently only .ktx (as it is a format the GPU likes)
    std::cout << "Loading shader...\n";
    Shader shader = renderer.load_shader("assets/shader.slang"); // The slang compiler is included in the library and shaders will be compiled when loaded
    std::cout << "Creating material...\n";
    // Create a material for gravel
    Material gravel_material(&gravel_texture, &shader); // Create a material from a texture and a shader

    // Create object(s) (mesh, material, position, rotation)
    Object monkey(&monkey_mesh, &gravel_material, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)); 
    Object monkey_right(&monkey_mesh, &gravel_material, glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f));
    Object monkey_left(&monkey_mesh, &gravel_material, glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f));
    Object monkey_up(&monkey_mesh, &gravel_material, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f));
    Object monkey_down(&monkey_mesh, &gravel_material, glm::vec3(0.0f, -2.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f));


    std::cout << "Adding object(s) to scene...\n";
    // Add objects to scene
    scene.add_object_to_scene(&monkey);
    scene.add_object_to_scene(&monkey_right);
    scene.add_object_to_scene(&monkey_left);
    scene.add_object_to_scene(&monkey_up);
    scene.add_object_to_scene(&monkey_down);

    scene.cam_pos = glm::vec3(0.0f, 0.0f, 10.0f);

    glm::vec3 camera_velocity(0.0f);
    float move_speed = 5.0f;
    float mouse_sensitivity = 0.0025f;
    float pitch = 0.0f;

    std::cout << "Starting rendering..." << std::endl;
    uint64_t last_time{ SDL_GetTicks() }; // this is only FPS metrics related stuff
    uint64_t fps_update_time{ last_time };
    uint32_t frame_count{ 0 };
    bool quit{ false };
    while (!quit) {
        uint64_t now = SDL_GetTicks();
        float elapsed_time{ (now - last_time) / 1000.0f };
        last_time = now;

        renderer.render_scene(scene); // Render the scene
        ++frame_count;
        // More FPS stuff
        if (now - fps_update_time >= 1000) {
            float fps{ static_cast<float>(frame_count) / ((now - fps_update_time) / 1000.0f) };
            std::cout << "FPS: " << fps << '\n';
            frame_count = 0;
            fps_update_time = now;
        }
        // Update your scene (e.g. rotate the monkeys)
        monkey.rotation.z += elapsed_time;
        monkey_right.rotation.y += elapsed_time;
        monkey_left.rotation.y -= elapsed_time;
        monkey_up.rotation.x += elapsed_time;
        monkey_down.rotation.x -= elapsed_time;

        // Input
        const bool* keys = SDL_GetKeyboardState(nullptr);

        camera_velocity = glm::vec3(0.0f);
        glm::mat4 camera_transform = glm::translate(
            glm::mat4(1.0f),
            scene.cam_pos
        ) * glm::mat4_cast(scene.cam_orientation);
        glm::vec3 forward = glm::normalize(glm::vec3(
            camera_transform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)
        ));
        glm::vec3 right = glm::normalize(glm::vec3(
            camera_transform * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)
        ));
        
        // Forward / backward
        if (keys[SDL_SCANCODE_W])
            camera_velocity.z += 1.0f;

        if (keys[SDL_SCANCODE_S])
            camera_velocity.z -= 1.0f;

        // Left / right
        if (keys[SDL_SCANCODE_A])
            camera_velocity.x -= 1.0f;

        if (keys[SDL_SCANCODE_D])
            camera_velocity.x += 1.0f;

        // Normalize so diagonal movement is not faster
        if (glm::length(camera_velocity) > 0.0f)
            camera_velocity = glm::normalize(camera_velocity);
    

        // Apply movement
        scene.cam_pos += forward * camera_velocity.z * move_speed * elapsed_time;
        scene.cam_pos += right * camera_velocity.x * move_speed * elapsed_time;

        if (keys[SDL_SCANCODE_ESCAPE]) {
            quit = true;
        }

        for (SDL_Event event; SDL_PollEvent(&event);) {
            // Exit loop if the application is about to close
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
                break;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                float mouse_x = (float)event.motion.xrel;
                float mouse_y = (float)event.motion.yrel;

                float next_pitch = glm::clamp(
                    pitch - mouse_y * mouse_sensitivity,
                    -glm::radians(89.0f),
                    glm::radians(89.0f)
                );
                float pitch_delta = next_pitch - pitch;
                pitch = next_pitch;

                scene.cam_orientation = glm::normalize(
                    glm::angleAxis(-mouse_x * mouse_sensitivity, glm::vec3(0.0f, 1.0f, 0.0f))
                    * scene.cam_orientation
                );
                glm::vec3 camera_right = scene.cam_orientation * glm::vec3(1.0f, 0.0f, 0.0f);
                scene.cam_orientation = glm::normalize(
                    glm::angleAxis(pitch_delta, camera_right)
                    * scene.cam_orientation
                );

                scene.cam_rot = glm::eulerAngles(scene.cam_orientation);
            }

            // Zooming with the mouse wheel 
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                scene.cam_pos += forward * (float)event.wheel.y * move_speed * 0.1f;
            }
        }
    }

    // If you plan on using the scene, but want to delete an object
    scene.remove_object_from_scene(&monkey); // only removes the object reference from the scene, does not delete the object

    // Destroy resources (you are fully responsible for the objects)
    renderer.destroy_mesh(monkey_mesh);
    renderer.destroy_texture(gravel_texture);
    renderer.destroy_shader(shader);

    renderer.destroy();

    // Alternativly let the OS do this (not nice or clean)

    std::cout << "Ending...\n";
    return 0;
}
