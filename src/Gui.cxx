#include "easy_pbr/Gui.h"

//opengl stuff
#include <glad/glad.h> // Initialize with gladLoadGL()
// Include glfw3.h after our OpenGL definitions
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_ext/IconsFontAwesome.h"
#include "imgui_ext/ImGuiUtils.h"
#include "imgui_ext/curve.hpp"
// #include "ImGuizmo.h"


//c++
#include <iostream>
#include <iomanip> // setprecision
// #include <experimental/filesystem>

//boost
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;


//My stuff
#include "Profiler.h"
#include "easy_pbr/Viewer.h"
#include "easy_pbr/MeshGL.h"
#include "easy_pbr/Scene.h"
#include "easy_pbr/Camera.h"
#include "easy_pbr/SpotLight.h"
#include "easy_pbr/Recorder.h"
#include "easy_pbr/LabelMngr.h"
#include "string_utils.h"
#include "eigen_utils.h"
#include "numerical_utils.h"
#include "se3_utils.h"

// //imgui
// #include "imgui.h"
// #include "imgui_impl_glfw.h"
// #include "imgui_impl_opengl3.h"
// #include "imgui_ext/curve.hpp"
// #include "imgui_ext/ImGuiUtils.h"
// #include <glad/glad.h> // Initialize with gladLoadGL()
// // Include glfw3.h after our OpenGL definitions
// #include <GLFW/glfw3.h>

//loguru
#define LOGURU_REPLACE_GLOG 1
#include <loguru.hpp>



using namespace radu::utils;


//configuru
#define CONFIGURU_WITH_EIGEN 1
#define CONFIGURU_IMPLICIT_CONVERSIONS 1
#include <configuru.hpp>
using namespace configuru;


namespace easy_pbr{

//redeclared things here so we can use them from this file even though they are static
// std::unordered_map<std::string, cv::Mat>  Gui::m_cv_mats_map;
// std::unordered_map<std::string, bool>  Gui::m_cv_mats_dirty_map;
std::unordered_map<std::string, WindowImg> Gui::m_win_imgs_map;
// std::unordered_map<std::string, NamedImg> Gui::m_named_imgs_map;
std::mutex  Gui::m_cv_mats_mutex;


Gui::Gui( const std::string config_file,
         Viewer* view,
         GLFWwindow* window
         ) :
        m_draw_main_menu(true),
        m_show_demo_window(false),
        m_show_profiler_window(true),
        m_show_player_window(true),
        m_selected_mesh_idx(0),
        m_selected_spot_light_idx(0),
        m_selected_trajectory_idx(0),
        m_mesh_tex_idx(0),
        m_show_debug_textures(false),
        m_guizmo_operation(ImGuizmo::TRANSLATE),
        m_guizmo_mode(ImGuizmo::LOCAL),
        m_traj_guizmo_operation(ImGuizmo::TRANSLATE),
        m_traj_guizmo_mode(ImGuizmo::LOCAL),
        m_trajectory_frustum_size(0.01f),
        m_hidpi_scaling(1.0),
        m_subsample_factor(0.5),
        m_decimate_nr_target_faces(100)
        // m_recording_path("./recordings/"),
        // m_snapshot_name("img.png"),
        // m_record_gui(false),
        // m_record_with_transparency(true)
         {
    m_view = view;


    init_params(config_file);

    m_imgui_context = ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    // const char* glsl_version = "#version 440";
    const char* glsl_version = "#version 330";
    ImGui_ImplOpenGL3_Init(glsl_version);

    init_style();

    //fonts and dpi things
    float font_size=13;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    //proggy
    std::string proggy_font_file=std::string(EASYPBR_DATA_DIR)+"/fonts/ProggyClean.ttf";
    if ( !fs::exists(proggy_font_file) ){
        LOG(FATAL) << "Couldn't find " << proggy_font_file;
    }
    io.Fonts->AddFontFromFileTTF(proggy_font_file.c_str(), font_size * m_hidpi_scaling);
    //awesomefont
    ImFontConfig config;
    config.MergeMode = true;
    // config.GlyphMinAdvanceX = -20.0f; //https://github.com/ocornut/imgui/issues/1869#issuecomment-395725056
    const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    std::string awesome_font_file=std::string(EASYPBR_DATA_DIR)+"/fonts/fontawesome-webfont.ttf";
    if ( !fs::exists(awesome_font_file) ){
        LOG(FATAL) << "Couldn't find " << awesome_font_file;
    }
    io.Fonts->AddFontFromFileTTF(awesome_font_file.c_str(), 17.0f*m_hidpi_scaling, &config, icon_ranges );

    //robot regular
    std::string roboto_regular_file=std::string(EASYPBR_DATA_DIR)+"/fonts/Roboto-Regular.ttf";
    CHECK(fs::exists(roboto_regular_file)) << "Couldn't find " << roboto_regular_file;
    m_roboto_regular=io.Fonts->AddFontFromFileTTF(roboto_regular_file.c_str(), 16.0f*m_hidpi_scaling);
    CHECK(m_roboto_regular!=nullptr) << "The font could not be loaded";

    //robot bold
    std::string roboto_bold_file=std::string(EASYPBR_DATA_DIR)+"/fonts/Roboto-Bold.ttf";
    CHECK(fs::exists(roboto_bold_file)) << "Couldn't find " << roboto_bold_file;
    m_roboto_bold=io.Fonts->AddFontFromFileTTF(roboto_bold_file.c_str(), 16.0f*m_hidpi_scaling);
    CHECK(m_roboto_bold!=nullptr) << "The font could not be loaded";


    io.Fonts->Build();
    //io.FontGlobalScale = 1.0 / pixel_ratio;
    ImGuiStyle *style = &ImGui::GetStyle();
    style->ScaleAllSizes(m_hidpi_scaling);


    m_curve_points[0].x = -1;

    //enable docking 
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    //prevent the moving of windows unless dragged by title bar. this is because we also want to drag inside the windows which have images in order to make cropping
    io.ConfigWindowsMoveFromTitleBarOnly=true;
}

void Gui::init_params(const std::string config_file){
    //read all the parameters
    // Config cfg = configuru::parse_file(std::string(CMAKE_SOURCE_DIR)+"/config/"+config_file, CFG);

    std::string config_file_trim=radu::utils::trim_copy(config_file);
    std::string config_file_abs;
    if (fs::path(config_file_trim).is_relative()){
        config_file_abs=fs::canonical(fs::path(PROJECT_SOURCE_DIR) / config_file_trim).string();
    }else{
        config_file_abs=config_file_trim;
    }


    //get all the default configs and all it's sections
    Config default_cfg = configuru::parse_file(std::string(DEFAULT_CONFIG), CFG);
    Config default_core_cfg=default_cfg["core"];

    //get the current config and if the section is not available, fallback to the default on
    Config cfg = configuru::parse_file(config_file_abs, CFG);
    Config core_cfg=cfg.get_or("core", default_cfg);
    bool is_hidpi = core_cfg.get_or("hidpi", default_core_cfg);

    m_hidpi_scaling= is_hidpi ? 2.0 : 1.0;
}

void Gui::select_mesh_with_idx(const int idx){
    m_selected_mesh_idx=idx;
}
int Gui::selected_mesh_idx(){
    return m_selected_mesh_idx;
}

void Gui::toggle_main_menu(){
    m_draw_main_menu^= 1;
}

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon fonts (see misc/fonts/README.txt)
void Gui::help_marker(const char* desc){
    // ImGui::TextDisabled("(?)");
    // ImGuiStyle *style = &ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.33f, 0.39f, 1.00f) );
    // ImGui::Text("(?)");
    ImVec2 pos = ImGui::GetCursorPos();
    pos.x -= 7;
    pos.y += 3;
    ImGui::SetCursorPos(pos);
    ImGui::Text(ICON_FA_QUESTION_CIRCLE);

    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Gui::update() {
    show_images();

    draw_label_mngr_legend();
    if(m_draw_main_menu){
        draw_main_menu();
    }
    draw_profiler();

    draw_overlays(); //draws stuff like the text indicating the vertices coordinates on top of the vertices in the 3D world

    draw_drag_drop_text(); //when the scene is empty draws the text saying to drag and drop something




    // 3. Show the ImGui test window. Most of the sample code is in ImGui::ShowTestWindow()
    if (m_show_demo_window) {
        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
        ImGui::ShowDemoWindow(&m_show_demo_window);
    }


}

void Gui::draw_main_menu(){

    ImVec2 canvas_size = ImGui::GetIO().DisplaySize;

    // ImGui::SetNextWindowSize(ImVec2(canvas_size.x*0.08*m_hidpi_scaling, canvas_size.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(310*m_hidpi_scaling, canvas_size.y), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    // ImGui::Begin("Menu", nullptr, main_window_flags);
    ImGui::Begin("Menu", nullptr,
            // ImGuiWindowFlags_NoTitleBar
            ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            // | ImGuiWindowFlags_NoScrollbar
            // | ImGuiWindowFlags_NoScrollWithMouse
            // | ImGuiWindowFlags_NoCollapse
            // | ImGuiWindowFlags_NoSavedSettings
            // | ImGuiWindowFlags_NoInputs
            );
    ImGui::PushItemWidth(135*m_hidpi_scaling);





    if (ImGui::CollapsingHeader("Viewer") ) {
        //combo of the data list with names for each of them

        if ( ImGui::Button("Hide all") ){
            Scene::hide_all();
        }
        ImGui::SameLine();
        if ( ImGui::Button("Show all") ){
            Scene::show_all();
        }
        ImGui::SameLine();
        if ( ImGui::Button("Clone") ){
            MeshSharedPtr mesh=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx);
            MeshSharedPtr clone  =  std::make_shared<Mesh>(mesh->clone());
            //find a name for it
            int clone_nr=1;
            for (int i=0; i<1000; i++){
                std::string clone_name=mesh->name+"_clone_"+std::to_string(clone_nr);
                // VLOG(1) << "checking ame" << clone_name;
                if (Scene::does_mesh_with_name_exist(clone_name)){
                    clone_nr++;
                }else{
                    //a mesh with this name doesnt exist so we just add it
                    VLOG(1) << "cloned with name "<<clone_name;
                    Scene::show(clone, clone_name);
                    break;
                }
            }

        }
        if(ImGui::ListBoxHeader("Scene meshes", Scene::nr_meshes(), 6)){
            for (int i = 0; i < Scene::nr_meshes(); ++i) {
                MeshSharedPtr mesh=m_view->m_scene->get_mesh_with_idx(i);

                //it's the one we have selected so we change the header color to a whiter value
                if(i==m_selected_mesh_idx){
                    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }


                //if the mesh is empty we display it in grey
                if(mesh->is_empty() ){
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.00f));  //gray text
                }else{
                    //visibility changes the text color from green to red
                    if(mesh->m_vis.m_is_visible){
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.7f, 0.1f, 1.00f));  //green text
                    }else{
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.1f, 0.1f, 1.00f)); //red text
                    }
                }




                if(ImGui::Selectable(mesh->name.c_str(), true, ImGuiSelectableFlags_AllowDoubleClick)){ //we leave selected to true so that the header appears and we can change it's colors
                    if (ImGui::IsMouseDoubleClicked(0)){
                        mesh->m_vis.m_is_visible=!mesh->m_vis.m_is_visible;
                        mesh->m_is_shadowmap_dirty=true;
                    }
                    m_selected_mesh_idx=i;
                }

                ImGui::PopStyleColor(2);

                //if we hover over a mesh, we display a tooltip with the information about it
                if (ImGui::IsItemHovered()){
                    std::string info="info";
                    ImVec2 tooltip_position;
                    tooltip_position.x = 310*m_hidpi_scaling;
                    tooltip_position.y = 0;
                    ImGui::SetNextWindowPos(tooltip_position);
                    // ImGui::SetNextWindowSize(ImVec2(150*m_hidpi_scaling, 200), ImGuiCond_Always);
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                    // ImGui::TextUnformatted(info.c_str());

                    auto &m = mesh;

                    ImGui::PushFont(m_roboto_regular);
                    // ImGui::Text(m->name.c_str());
                    // if (m->V.size()){
                    if(!m->V.size()){
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.1f, 0.1f, 1.00f)); //red text
                        ImGui::PushFont(m_roboto_bold);
                    }
                    ImGui::TextUnformatted(  ( m->V.size()? ( "V:" + std::to_string(m->V.rows()) + " x " + std::to_string(m->V.cols()))  : "V: empty"  ).c_str()  );
                    if(!m->V.size()){
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    }
                    ImGui::TextUnformatted(  ( m->F.size()? ( "F:" + std::to_string(m->F.rows()) + " x " + std::to_string(m->F.cols()))  : "F: empty"  ).c_str()  );
                    ImGui::TextUnformatted(  ( m->E.size()? ( "E:" + std::to_string(m->E.rows()) + " x " + std::to_string(m->E.cols()))  : "E: empty"  ).c_str()  );
                    ImGui::TextUnformatted(  ( m->C.size()? ( "C:" + std::to_string(m->C.rows()) + " x " + std::to_string(m->C.cols()))  : "C: empty"  ).c_str()  );
                    ImGui::TextUnformatted(  ( m->D.size()? ( "D:" + std::to_string(m->D.rows()) + " x " + std::to_string(m->D.cols()))  : "D: empty"  ).c_str()  );
                    ImGui::TextUnformatted(  ( m->NV.size()? ( "NV:" + std::to_string(m->NV.rows()) + " x " + std::to_string(m->NV.cols()))  : "NV: empty"  ).c_str()  );
                    ImGui::TextUnformatted(  ( m->NF.size()? ( "NF:" + std::to_string(m->NF.rows()) + " x " + std::to_string(m->NF.cols()))  : "NF: empty"  ).c_str()  );
                    ImGui::TextUnformatted(  ( m->UV.size()? ( "UV:" + std::to_string(m->UV.rows()) + " x " + std::to_string(m->UV.cols()))  : "UV: empty"  ).c_str()  );


                    // }
                    // ImGui::PushFont(m_roboto_bold);
                    // ImGui::Text("Hello!");
                    ImGui::PopFont();

                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }


            }
            ImGui::ListBoxFooter();
        }


        if(!m_view->m_scene->is_empty() ){ //if the scene is empty there will be no mesh to select
            MeshSharedPtr mesh=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx);
            ImGui::InputText("Name", mesh->name );
            if( ImGui::Checkbox("Show points", &mesh->m_vis.m_show_points)) { mesh->m_is_shadowmap_dirty=true;  }
                ImGui::Indent(10.0f*m_hidpi_scaling);
                ImGui::Checkbox("Overlay points", &mesh->m_vis.m_overlay_points);  ImGui::SameLine(); help_marker("Draws the points even if they are occluded");
                ImGui::Checkbox("Points as circle", &mesh->m_vis.m_points_as_circle); ImGui::SameLine(); help_marker("Draws points as circles instad of squares. Moderate performance impact.");
                ImGui::Unindent(10.0f*m_hidpi_scaling );
            if( ImGui::Checkbox("Show lines", &mesh->m_vis.m_show_lines) ) { mesh->m_is_shadowmap_dirty=true;  }
                ImGui::Indent(10.0f*m_hidpi_scaling);  ImGui::Checkbox("Overlay lines", &mesh->m_vis.m_overlay_lines); ImGui::SameLine(); help_marker("Draws the lines even if they are occluded");
                ImGui::Unindent(10.0f*m_hidpi_scaling );
            if( ImGui::Checkbox("Show mesh", &mesh->m_vis.m_show_mesh) ) {  mesh->m_is_shadowmap_dirty=true;  }
            if( ImGui::Checkbox("Show normals", &mesh->m_vis.m_show_normals) ) {  mesh->m_is_shadowmap_dirty=true;  }
            if ( mesh->m_vis.m_show_normals ) { ImGui::SliderFloat("Normal_scale", &mesh->m_vis.m_normals_scale, -1.0f, 1.0f) ;  }
            if( ImGui::Checkbox("Show wireframe", &mesh->m_vis.m_show_wireframe)) {  mesh->m_is_shadowmap_dirty=true; }
            if( ImGui::Checkbox("Show surfels", &mesh->m_vis.m_show_surfels) ) { mesh->m_is_shadowmap_dirty=true; }
            if( ImGui::Checkbox("Custom shader", &mesh->m_vis.m_use_custom_shader )){
                //check that we have defined a custom rendering function
                if(!mesh->custom_render_func){
                    LOG(WARNING) << "There is no custom render function for this mesh. Please assign one by using mesh->custom_render_func=foo";
                    mesh->m_vis.m_use_custom_shader=false;
                }
                mesh->m_is_shadowmap_dirty=true;
            }
            ImGui::Checkbox("Show vert ids", &mesh->m_vis.m_show_vert_ids);
            ImGui::SameLine(); help_marker("Shows the indexes that each vertex has within the V matrix, \n i.e. the row index");
            ImGui::Checkbox("Show vert coords", &mesh->m_vis.m_show_vert_coords);
            ImGui::SameLine(); help_marker("Shows the coordinates in XYZ for each vertex");
            ImGui::SliderFloat("Line width", &mesh->m_vis.m_line_width, 0.6f, 5.0f);
            ImGui::SliderFloat("Point size", &mesh->m_vis.m_point_size, 1.0f, 20.0f);

            std::string current_selected_str=mesh->m_vis.m_color_type._to_string();
            MeshColorType current_selected=mesh->m_vis.m_color_type;
            if (ImGui::BeginCombo("Mesh color type", current_selected_str.c_str(), ImGuiComboFlags_HeightLarge  )) { // The second parameter is the label previewed before opening the combo.
                for (size_t n = 0; n < MeshColorType::_size(); n++) {
                    bool is_selected = ( current_selected == MeshColorType::_values()[n] );
                    if (ImGui::Selectable( MeshColorType::_names()[n], is_selected)){

                        mesh->m_vis.m_color_type= MeshColorType::_values()[n]; //select this one because we clicked on it
                        //sanity checks
                        //color
                        if( mesh->m_vis.m_color_type==+MeshColorType::PerVertColor && !mesh->C.size() ){
                            LOG(WARNING) << "There is no color per vertex associated to the mesh. Please assign some data to the mesh.C matrix.";
                        }
                        //semannticgt and semanticpred
                        if( (mesh->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_label_mngr  ){
                            LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
                        }
                        if( mesh->m_vis.m_color_type==+MeshColorType::NormalVector && !mesh->NV.size() ){
                            LOG(WARNING) << "There is no normal per vertex associated to the mesh. Please assign some data to the mesh.NV matrix.";
                        }
                        if( mesh->m_vis.m_color_type==+MeshColorType::Intensity && !mesh->I.size() ){
                            LOG(WARNING) << "There is no intensity per vertex associated to the mesh. Please assign some data to the mesh.I matrix.";
                        }
                        //check if we actually have a texture
                        if( mesh->m_vis.m_color_type==+MeshColorType::Texture){
                            std::shared_ptr<MeshGL> mesh_gl = mesh->m_mesh_gpu.lock();
                            if(mesh_gl && !mesh_gl->m_diffuse_tex.storage_initialized() ){
                                LOG(WARNING) << "There is no texture associated to the mesh. Please assign some texture by using set_diffuse_tex()";
                            }
                        }
                        // if(mesh->m_mesh_gpu && !mesh->m_mesh_gpu->m_cur_tex_ptr.storage_initialized()){
                            // LOG(WARNING) << "There is no texture associated to the mesh";
                        // }

                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();   // Set the initial focus when opening the combo (scrolling + for keyboard navigation support in the upcoming navigation branch)
                }
                ImGui::EndCombo();
            }
            // //if its texture then we cna choose the texture type
            // if( ImGui::SliderInt("texture_type", &m_mesh_tex_idx, 0, 2) ){
            //     if (auto mesh_gpu =  mesh->m_mesh_gpu.lock()) {
            //         if(m_mesh_tex_idx==0){
            //             mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_rgb_tex;
            //         }else if(m_mesh_tex_idx==1){
            //             mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_tex;
            //         }else if(m_mesh_tex_idx==2){
            //             mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_colored_tex;
            //         }
            //     }
            // }


            ImGui::ColorEdit3("Mesh color",mesh->m_vis.m_solid_color.data());
            ImGui::ColorEdit3("Point color",mesh->m_vis.m_point_color.data());
            ImGui::ColorEdit3("Line color",mesh->m_vis.m_line_color.data());
            ImGui::ColorEdit3("Label color",mesh->m_vis.m_label_color.data());
            //show the textures
            m_diffuse_tex_hovered=false;
            m_normals_tex_hovered=false;
            m_metalness_tex_hovered=false;
            m_roughness_tex_hovered=false;
            auto mesh_gpu= mesh->m_mesh_gpu.lock();
            if(mesh_gpu){
                ImGui::Columns(4, "Textures", false);  // 4-colmns, no border
                ImGui::Separator();

                //diffuse
                ImVec2 size = ImVec2(50*m_hidpi_scaling,50*m_hidpi_scaling);
                if ( mesh_gpu->m_diffuse_tex.storage_initialized() ){
                    ImGui::Image( (ImTextureID)(uintptr_t)mesh_gpu->m_diffuse_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }else{
                    ImGui::Image( (ImTextureID)(uintptr_t)m_view->m_uv_checker_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }
                if (ImGui::IsItemHovered()){ m_diffuse_tex_hovered=true; }

                //normals
                 if ( mesh_gpu->m_normals_tex.storage_initialized() ){
                    ImGui::Image( (ImTextureID)(uintptr_t)mesh_gpu->m_normals_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }else{
                    ImGui::Image( (ImTextureID)(uintptr_t)m_view->m_uv_checker_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }
                 if (ImGui::IsItemHovered()){ m_normals_tex_hovered=true; }

                //metalness
                if ( mesh_gpu->m_metalness_tex.storage_initialized() ){
                    ImGui::Image( (ImTextureID)(uintptr_t)mesh_gpu->m_metalness_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }else{
                    ImGui::Image( (ImTextureID)(uintptr_t)m_view->m_uv_checker_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }
                 if (ImGui::IsItemHovered()){ m_metalness_tex_hovered=true; }

                //roughness
                if ( mesh_gpu->m_roughness_tex.storage_initialized() ){
                    ImGui::Image( (ImTextureID)(uintptr_t)mesh_gpu->m_roughness_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }else{
                    ImGui::Image( (ImTextureID)(uintptr_t)m_view->m_uv_checker_tex.tex_id(), size, ImVec2(0,1), ImVec2(1,0)); ImGui::NextColumn();
                }
                if (ImGui::IsItemHovered()){ m_roughness_tex_hovered=true; }

                // ImGui::SetNextItemWidth(51);

                ImGui::Text(  "Diffuse"  ); ImGui::NextColumn();
                ImGui::Text(  "Normals"  ); ImGui::NextColumn();
                ImGui::Text(  "Metal"  ); ImGui::NextColumn();
                ImGui::Text(  "Rough"  );  ImGui::NextColumn();

                ImGui::Columns(1);
                ImGui::Separator();
            }




            ImGui::SliderFloat("Metalness", &mesh->m_vis.m_metalness, 0.0f, 1.0f) ;
            ImGui::SliderFloat("Roughness", &mesh->m_vis.m_roughness, 0.0f, 1.0f  );


            //min max in y for plotting height of point clouds
            if(mesh->m_vis.m_color_type==+MeshColorType::Height){
                float min_y=mesh->m_min_max_y(0); //this is the real min_y of the data
                float max_y=mesh->m_min_max_y(1);
                float min_y_plotting = mesh->m_min_max_y_for_plotting(0); //this is the min_y that the viewer sees when plotting the mesh
                float max_y_plotting = mesh->m_min_max_y_for_plotting(1);
                ImGui::SliderFloat("min_y", &mesh->m_min_max_y_for_plotting(0), min_y, std::min(max_y, max_y_plotting) );
                ImGui::SameLine(); help_marker("tooltip");
                ImGui::SliderFloat("max_y", &mesh->m_min_max_y_for_plotting(1), std::max(min_y_plotting, min_y), max_y);
            }


        }

        ImGui::SliderFloat("surfel_blend_factor", &m_view->m_surfel_blend_factor, -300, 300 );
        ImGui::SliderFloat("surfel_blend_scale", &m_view->m_surfel_blend_scale, -300, 300 );


        ImGui::ColorEdit3("BG color",m_view->m_background_color.data());
        ImGui::ColorEdit3("Ambient color",m_view->m_ambient_color.data());
        ImGui::SliderFloat("Ambient power", &m_view->m_ambient_color_power, 0.0f, 1.0f);


        //tonemapper
        std::string current_selected_str=m_view->m_tonemap_type._to_string();
        ToneMapType current_selected=m_view->m_tonemap_type;
        if (ImGui::BeginCombo("Tonemap type", current_selected_str.c_str(), ImGuiComboFlags_HeightLarge  )) { // The second parameter is the label previewed before opening the combo.
            for (size_t n = 0; n < ToneMapType::_size(); n++) {
                bool is_selected = ( current_selected == ToneMapType::_values()[n] );
                if (ImGui::Selectable( ToneMapType::_names()[n], is_selected)){

                    m_view->m_tonemap_type= ToneMapType::_values()[n]; //select this one because we clicked on it

                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();   // Set the initial focus when opening the combo (scrolling + for keyboard navigation support in the upcoming navigation branch)
            }
            ImGui::EndCombo();
        }


        ImGui::Checkbox("Enable LightFollow", &m_view->m_lights_follow_camera);
        ImGui::Checkbox("Enable culling", &m_view->m_enable_culling);
        ImGui::SameLine(); help_marker("Hides the mesh faces that are pointing away from the viewer. Offers a mild increase in performance.");
        ImGui::Checkbox("Enable SSAO", &m_view->m_enable_ssao);
        ImGui::SameLine(); help_marker("Screen Space Ambient Occlusion. Darkens crevices and corners in the mesh in order to better show the details. It has a mild impact on performance.");
        ImGui::Checkbox("Enable EDL", &m_view->m_enable_edl_lighting);
        ImGui::SameLine(); help_marker("Eye Dome Lighting. Useful for rendering point clouds which are devoid of normal vectors. Darkens the pixels according to aparent change in depth of the neighbouring pixels.");
        if(m_view->m_enable_edl_lighting){
            ImGui::SliderFloat("EDL strength", &m_view->m_edl_strength, 0.0f, 50.0f);
        }
        ImGui::Checkbox("Enable Bloom", &m_view->m_enable_bloom);
        ImGui::SameLine(); help_marker("Bleed the highly bright areas of the scene onto the adjacent pixels. High performance cost.");
        ImGui::Checkbox("Enable IBL", &m_view->m_enable_ibl);
        ImGui::SameLine(); help_marker("Image Based Ligthing. Uses and HDR environment map to light the scene instead of just spotlights.\nProvides a good sense of inmersion and makes the object look like they belong in a certain scene.");
        ImGui::Checkbox("Show Environment", &m_view->m_show_environment_map);
        // if(m_view->m_show_environment_map);
            // ImGui::SliderFloat("environment_map_blur", &m_view->m_environment_map_blur, 0.0, m_view->m_prefilter_cubemap_tex.mipmap_nr_lvls() );
        // }
        ImGui::Checkbox("Show Blurry Environment", &m_view->m_show_prefiltered_environment_map);
        if(m_view->m_show_prefiltered_environment_map){
            ImGui::SliderFloat("environment_map_blur", &m_view->m_environment_map_blur, 0.0, m_view->m_prefilter_cubemap_tex.mipmap_nr_lvls() );
        }


        ImGui::Separator();
        if (ImGui::CollapsingHeader("Move") && !m_view->m_scene->is_empty() ) {
            if (ImGui::RadioButton("Trans", m_guizmo_operation == ImGuizmo::TRANSLATE)) { m_guizmo_operation = ImGuizmo::TRANSLATE; } ImGui::SameLine();
            if (ImGui::RadioButton("Rot", m_guizmo_operation == ImGuizmo::ROTATE)) { m_guizmo_operation = ImGuizmo::ROTATE; } ImGui::SameLine();
            if (ImGui::RadioButton("Scale", m_guizmo_operation == ImGuizmo::SCALE)) { m_guizmo_operation = ImGuizmo::SCALE; }  // is fucked up because after rotating I cannot hover over the handles

            if (ImGui::RadioButton("Local", m_guizmo_mode == ImGuizmo::LOCAL)) { m_guizmo_mode = ImGuizmo::LOCAL; } ImGui::SameLine();
            if (ImGui::RadioButton("World", m_guizmo_mode == ImGuizmo::WORLD)) { m_guizmo_mode = ImGuizmo::WORLD; }

            if (ImGui::Button("Copy Pose")){
                Eigen::Affine3d model_matrix=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->model_matrix();
                std::vector<std::string> pose_vec=radu::utils::tf_matrix2vecstring(model_matrix);
                std::string pose_string=radu::utils::join(pose_vec, " ");
                VLOG(1) << "Copied model matrix to clipboard: " << pose_string;
                glfwSetClipboardString(m_view->m_window, pose_string.c_str());
            }

            edit_transform(m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx));
        }


    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("MeshOps")) {
        if (ImGui::Button("worldGL2worldROS") && !m_view->m_scene->is_empty() ){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->worldGL2worldROS();
        }
        if (ImGui::Button("worldROS2worldGL") && !m_view->m_scene->is_empty() ){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->worldROS2worldGL();
        }
        if (ImGui::Button("Rotate_90") && !m_view->m_scene->is_empty() ){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->rotate_90_x_axis();
        }
        ImGui::SliderFloat("rand subsample_factor", &m_subsample_factor, 0.0, 1.0);
        if (ImGui::Button("Rand subsample") && !m_view->m_scene->is_empty() ){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->random_subsample(m_subsample_factor);
        }
        int nr_faces= m_view->m_scene->is_empty() ? 0 : m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->F.rows();
        ImGui::SliderInt("decimate_nr_faces", &m_decimate_nr_target_faces, 1, nr_faces);
        if (ImGui::Button("Decimate") && !m_view->m_scene->is_empty() ){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->decimate(m_decimate_nr_target_faces);
        }
        if (ImGui::Button("Flip normals") && !m_view->m_scene->is_empty() ){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->flip_normals();
        }

        if (ImGui::Button("Merge all meshes")){
            //go through every mesh, apply the model matrix transform to the cpu vertices and then set the model matrix to identity,
            //afterards recrusivelly run .add() on the first mesh with all the others

            MeshSharedPtr mesh_merged= Mesh::create();

            for(int i=0; i<Scene::nr_meshes(); i++){
                MeshSharedPtr mesh=m_view->m_scene->get_mesh_with_idx(i);
                if(mesh->name!="grid_floor"){
                    mesh->transform_vertices_cpu(mesh->model_matrix());
                    mesh->set_model_matrix( Eigen::Affine3d::Identity() );
                    mesh_merged->add(*mesh);
                }
            }

            Scene::show(mesh_merged, "merged");

            // m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->flip_normals();
        }

        if (ImGui::Button("AddCube") ){
            std::shared_ptr<easy_pbr::Mesh> mesh=Mesh::create();
            mesh->create_box(1,1,1);
            Scene::show(mesh,"cube");
        }

        if (ImGui::Button("AddSphere") ){
            std::shared_ptr<easy_pbr::Mesh> mesh=Mesh::create();
            Eigen::Vector3d center;
            center.setZero();
            mesh->create_sphere(center, 1.0);
            Scene::show(mesh,"sphere");
        }


    }



    ImGui::Separator();
    if (ImGui::CollapsingHeader("SSAO")) {
        ImGui::SliderInt("Downsample", &m_view->m_ssao_downsample, 0, 5);
        ImGui::SameLine(); help_marker("Calculates the Screen Space Ambient Occlusion at a lower resolution dicated by the downsample factor. The higher the downsample factor the faster but also the blockier the ambient occlusion is.");
        ImGui::SliderFloat("Radius", &m_view->m_kernel_radius, 0.1, 100.0);
        ImGui::SameLine(); help_marker("Radius of the hemishpere around which to check for occlusions for each pixel. Higher values causes the occlusion to affect greater areas and lower value concentrates the samples on small details. Too high of a radius negativelly affects performance.");
        if( ImGui::SliderInt("Nr. samples", &m_view->m_nr_samples, 8, 255) ){
            m_view->create_random_samples_hemisphere();
        }
        ImGui::SameLine(); help_marker("Nr of random samples to check for occlusion around the hemisphere of each pixel. The higher the number the higher the accuracy of the occlusion but also the slower it is to compute.");
        ImGui::SliderInt("AO power", &m_view->m_ao_power, 1, 15);
        ImGui::SliderFloat("Sigma S", &m_view->m_sigma_spacial, 1, 12.0);
        ImGui::SameLine(); help_marker("The SSAO map is blurred with a bilateral blur with a sigma in the spacial dimension and in the depth. This is the sigma in the spacial dimension and higher values yield blurrier AO.");
        ImGui::SliderFloat("Sigma D", &m_view->m_sigma_depth, 0.1, 5.0);
        ImGui::SameLine(); help_marker("The SSAO map is blurred with a bilateral blur with a sigma in the spacial dimension and in the depth. This is the sigma in depth so as to avoid blurring over depth discontinuities. The higher the value, the more tolerant the blurring is to depth discontinuities.");
        ImGui::Checkbox("Estimate_normals_from_depth", &m_view->m_ssao_estimate_normals_from_depth);
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Bloom")) {
        ImGui::SliderFloat("BloomThresh", &m_view->m_bloom_threshold, 0.0, 2.0);
        ImGui::SameLine(); help_marker("Threshold over which pixels get classified as being bright enough to be bloomed. The lower the value the more pixels are bloomed. Has no impact on performance");
        ImGui::SliderInt("BloomStartMipMap", &m_view->m_bloom_start_mip_map_lvl, 0, 6);
        ImGui::SliderInt("BloomMaxMipMap", &m_view->m_bloom_max_mip_map_lvl, 0, 6);
        ImGui::SameLine(); help_marker("Bloom is applied hierarchically over multiple mip map levels. We use as many mip maps as specified by this value.");
        ImGui::SliderInt("BloomBlurIters", &m_view->m_bloom_blur_iters, 0, 10);
        ImGui::SameLine(); help_marker("Bloom is applied multiple times for each mip map level. The higher the value the more spreaded the bloom is. Has a high impact on performance.");
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("MultiChannel")) {
        ImGui::Checkbox("Enable multichannel view", &m_view->m_enable_multichannel_view);
        ImGui::SliderFloat("interline_separation", &m_view->m_multichannel_interline_separation, 0.0, 1.0);
        ImGui::SliderFloat("line_width", &m_view->m_multichannel_line_width, 0.0, 30.0);
        ImGui::SliderFloat("line_angle", &m_view->m_multichannel_line_angle, -90.0, 90.0);
        ImGui::SliderFloat("start_x", &m_view->m_multichannel_start_x, -2000.0, 2000.0);
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Camera")) {
        //select the camera, either the defalt camera or one of the point lights
        if(ImGui::ListBoxHeader("Enabled camera", m_view->m_spot_lights.size()+1, 6)){ //all the spot

            //push the text for the default camera
            if(m_view->m_camera==m_view->m_default_camera){
                ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
            }else{
                ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
            }
            if(ImGui::Selectable("Default cam", true)){
                m_view->m_camera=m_view->m_default_camera;
            }
            ImGui::PopStyleColor(1);

            //push camera selection box for the spot lights
            for (int i = 0; i < (int)m_view->m_spot_lights.size(); i++) {
                if(m_view->m_camera==m_view->m_spot_lights[i]){
                    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }
                if(ImGui::Selectable( ("SpotLight_"+std::to_string(i)).c_str(), true ) ){
                    m_view->m_camera=m_view->m_spot_lights[i];
                }
                ImGui::PopStyleColor(1);
            }
            ImGui::ListBoxFooter();
        }


        ImGui::SliderFloat("FOV", &m_view->m_camera->m_fov, 30.0, 120.0);
        ImGui::SliderFloat("near", &m_view->m_camera->m_near, 0.01, 10.0);
        ImGui::SliderFloat("far", &m_view->m_camera->m_far, 100.0, 5000.0);
        ImGui::SliderFloat("Exposure", &m_view->m_camera->m_exposure, 0.1, 10.0);

        ImGui::SliderFloat("Translation_speed", &m_view->m_camera_translation_speed_multiplier, 0.0, 10);

    }

    ImGui::Separator();
    if ( ImGui::CollapsingHeader("ViewFollower") )
    {
        const std::string trajectory_mesh_name="trajectory";
        const std::string frustum_mesh_name="frustum";

        if(ImGui::ListBoxHeader("Trajectory", m_view->m_trajectory.size(), 6)){
            for (int i = 0; i < (int)m_view->m_trajectory.size(); ++i) {

                //it's the one we have selected so we change the header color to a whiter value
                if(i==m_selected_trajectory_idx){
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }

                //visibility changes the text color from green to red
                if(m_view->m_trajectory[i]->m_traj.m_enabled){
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.7f, 0.1f, 1.00f));  //green text
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.1f, 0.1f, 1.00f)); //red text
                }
                if(ImGui::Selectable( ("Pose_"+std::to_string(i)).c_str(), true ) ){
                    m_selected_trajectory_idx=i;
                }
                // add code for manipulation
                ImGui::PopStyleColor(2);
            }
            ImGui::ListBoxFooter();
            if ( ! m_view->m_trajectory.empty() )
            {
                if (ImGui::Button("Move Up")){
                    if ( m_selected_trajectory_idx > 0 ){
                        std::shared_ptr<Camera> tmpCam = m_view->m_trajectory[m_selected_trajectory_idx-1];
                        m_view->m_trajectory[m_selected_trajectory_idx-1] = m_view->m_trajectory[m_selected_trajectory_idx];
                        m_view->m_trajectory[m_selected_trajectory_idx] = tmpCam;
                        --m_selected_trajectory_idx;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Move Down")){
                    if ( m_selected_trajectory_idx+1 < (int)m_view->m_trajectory.size() ){
                        std::shared_ptr<Camera> tmpCam = m_view->m_trajectory[m_selected_trajectory_idx+1];
                        m_view->m_trajectory[m_selected_trajectory_idx+1] = m_view->m_trajectory[m_selected_trajectory_idx];
                        m_view->m_trajectory[m_selected_trajectory_idx] = tmpCam;
                        ++m_selected_trajectory_idx;
                    }
                }
                ImGui::Checkbox("Enabled", &m_view->m_trajectory[m_selected_trajectory_idx]->m_traj.m_enabled);

                ImGui::Checkbox("Draw Trajectory", &m_traj_should_draw);
                if ( m_traj_should_draw )
                {
                    draw_trajectory( trajectory_mesh_name, frustum_mesh_name );
                }
                else
                {
                    if ( m_view->m_scene->does_mesh_with_name_exist( trajectory_mesh_name) )
                        m_view->m_scene->remove_mesh_with_idx(m_view->m_scene->get_idx_for_name(trajectory_mesh_name));
                    if ( m_view->m_scene->does_mesh_with_name_exist( frustum_mesh_name) )
                        m_view->m_scene->remove_mesh_with_idx(m_view->m_scene->get_idx_for_name(frustum_mesh_name));
                }

                if (ImGui::RadioButton("Use Time", m_traj_use_time_not_frames)) { m_traj_use_time_not_frames = true; } ImGui::SameLine();
                if (ImGui::RadioButton("Use Frames", ! m_traj_use_time_not_frames)) { m_traj_use_time_not_frames = false; }
                ImGui::SliderInt("FPS", &m_traj_fps, 1, 100);
                ImGui::SliderFloat("Transition Duration", &m_view->m_trajectory[m_selected_trajectory_idx]->m_traj.m_transition_duration, 0.01, 100.0);
                ImGui::SliderFloat("Frustum size", &m_trajectory_frustum_size, 0.0001, 2.0);

            }
        }
        ImGui::InputText("trajectory_file", m_traj_file_name);
        if (ImGui::Button("Load"))
        {
            if ( fs::exists(m_traj_file_name) )
            {
                std::ifstream file ( m_traj_file_name );
                if ( ! file.is_open() )
                    LOG(FATAL) << "file not opened: " << m_traj_file_name;
                m_traj_is_playing = false;
                m_view->m_trajectory.clear();
                m_selected_trajectory_idx = 0;
                double ts;
                bool enabled;
                while ( file.good() )
                {
                    file >> ts >> enabled;
                    std::string line;
                    if ( ! std::getline(file, line) ) continue;
                    std::shared_ptr<Camera> new_camera = m_view->m_default_camera->clone();
                    new_camera->from_string(line);
                    new_camera->m_traj.m_enabled = enabled;
                    new_camera->m_traj.m_transition_duration = ts;
                    m_view->m_trajectory.emplace_back(new_camera);
                }
                file.close();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            std::ofstream file ( m_traj_file_name );
            if ( ! file.is_open() )
                LOG(FATAL) << "file not opened: " << m_traj_file_name;
            for ( std::shared_ptr<Camera> & cam : m_view->m_trajectory )
            {
                file << cam->m_traj.m_transition_duration << " " << cam->m_traj.m_enabled << " " << cam->to_string() <<"\n";
            }
            file.close();
        }
        if (ImGui::Button("Add View")){
            m_view->m_trajectory.emplace_back(std::make_shared<Camera>());
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy View")){
            if ( ! m_view->m_trajectory.empty() )
                m_view->m_trajectory.emplace_back(m_view->m_trajectory[m_selected_trajectory_idx]->clone());
        }
        ImGui::SameLine();
        if (ImGui::Button("From View")){
            m_view->m_trajectory.emplace_back(m_view->m_camera->clone());
        }
        if (ImGui::Button("Remove View")){
            if ( !m_view->m_trajectory.empty() )
            {
                m_view->m_trajectory.erase(m_view->m_trajectory.begin()+m_selected_trajectory_idx);
                m_selected_trajectory_idx = std::max(m_selected_trajectory_idx-1,0);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Views"))
        {
            m_traj_is_playing = false;
            m_view->m_trajectory.clear();
            m_selected_trajectory_idx = 0;
            if ( m_view->m_scene->does_mesh_with_name_exist( trajectory_mesh_name) )
                m_view->m_scene->remove_mesh_with_idx(m_view->m_scene->get_idx_for_name(trajectory_mesh_name));
            if ( m_view->m_scene->does_mesh_with_name_exist( frustum_mesh_name) )
                m_view->m_scene->remove_mesh_with_idx(m_view->m_scene->get_idx_for_name(frustum_mesh_name));
        }
        if (ImGui::Button("Set View"))
        {
            m_traj_is_playing = false;
            if ( ! m_view->m_trajectory.empty() )
                m_view->m_camera = m_view->m_trajectory[m_selected_trajectory_idx]->clone();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset View"))
        {
            m_view->m_camera = m_view->m_default_camera;
            m_traj_is_playing = false;
        }
        bool should_play_trajectory = false;
        if (ImGui::Button("Play"))
        {
            if ( ! m_view->m_trajectory.empty() )
                should_play_trajectory = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            m_traj_is_paused = false;
            m_traj_is_playing = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause"))
        {
            m_traj_is_paused = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Preview"))
        {
            if ( ! m_view->m_trajectory.empty() )
            {
                should_play_trajectory = true;
                m_traj_preview = true;
            }
        }
        if ( should_play_trajectory )
        {
            m_view->m_timer->start();
            if ( ! m_traj_preview )
                m_view->m_camera = m_view->m_trajectory[m_selected_trajectory_idx]->clone();
            else
                m_preview_camera = m_view->m_trajectory[m_selected_trajectory_idx]->clone();
            m_traj_is_playing = true;
            m_traj_is_paused = false;
        }
        if ( m_traj_is_playing && !m_traj_is_paused && !m_view->m_trajectory.empty() )
        {
            ++m_traj_view_updates;
            static double last_ts = m_view->m_timer->elapsed_s();
            double ts = m_view->m_timer->elapsed_s();
            std::shared_ptr<Camera> cur_camera = m_traj_preview ? m_preview_camera : m_view->m_camera;

            // should switch to next:
            const bool need_to_update = m_traj_use_time_not_frames ? ts < cur_camera->m_traj.m_transition_duration : m_traj_view_updates < m_traj_fps * cur_camera->m_traj.m_transition_duration;
            if (  need_to_update )
            {
                if ( m_selected_trajectory_idx+1 < (int)m_view->m_trajectory.size() )
                {
                    // interpolate for current one the model_matrix
                    const double maxT = m_traj_use_time_not_frames ? cur_camera->m_traj.m_transition_duration : m_traj_fps * cur_camera->m_traj.m_transition_duration;
                    const double dt = m_traj_use_time_not_frames ? (ts - last_ts) : 1. ;
                    const double lastT = m_traj_use_time_not_frames ? last_ts : m_traj_view_updates;
                    const double newT = std::min(1., std::max(0., dt / ( maxT - lastT + dt )));
                    const Eigen::Affine3d curModelMatrix ( cur_camera->model_matrix().cast<double>() );
                    const Eigen::Affine3d desiredModelMatrix (m_view->m_trajectory[m_selected_trajectory_idx+1]->model_matrix().cast<double>() );
                    const Eigen::Affine3d nextModelMatrix = interpolateSE3(curModelMatrix,desiredModelMatrix,newT);
                    const Eigen::Affine3d delta = nextModelMatrix * curModelMatrix.inverse();

                    cur_camera->transform_model_matrix(delta.cast<float>());
                    cur_camera->m_fov = (1-newT) * cur_camera->m_fov + newT * m_view->m_trajectory[m_selected_trajectory_idx+1]->m_fov;
                    cur_camera->m_near = (1-newT) * cur_camera->m_near + newT * m_view->m_trajectory[m_selected_trajectory_idx+1]->m_near;
                    cur_camera->m_far = (1-newT) * cur_camera->m_far + newT * m_view->m_trajectory[m_selected_trajectory_idx+1]->m_far;

                    if ( m_traj_preview )
                    {
                        // show preview of frustum
                        MeshSharedPtr frustum_mesh = cur_camera->create_frustum_mesh( m_trajectory_frustum_size, m_view->m_viewport_size);
                        frustum_mesh->C.setConstant(0);
                        frustum_mesh->C.col(0).setConstant(1);
                        frustum_mesh->m_vis.m_point_size = 20;
                        frustum_mesh->m_vis.m_line_width = 5;
                        frustum_mesh->m_vis.set_color_pervertcolor();
                        frustum_mesh->m_vis.m_show_points=true;
                        frustum_mesh->m_vis.m_show_mesh=false;
                        frustum_mesh->m_vis.m_show_lines=true;
                        Scene::show(frustum_mesh,"cur_frustrum");
                    }
                }
            }
            else
            {
                m_view->m_timer->stop();
                if ( m_selected_trajectory_idx+1 < (int)m_view->m_trajectory.size() )
                {
                    // next step
                    if ( ! m_traj_preview )
                        m_view->m_camera = m_view->m_trajectory[m_selected_trajectory_idx+1]->clone();
                    else
                        m_preview_camera = m_view->m_trajectory[m_selected_trajectory_idx+1]->clone();
                    m_view->m_timer->start();
                    m_traj_view_updates = 0;
                    ts = 0;
                    m_selected_trajectory_idx++;
                }
                else
                {
                    // stop:
                    m_traj_is_playing = false;
                    m_traj_is_paused = false;
                    m_traj_preview = false;
                }
            }
            last_ts = ts;
        }
        else
        {
            if ( !m_traj_is_paused )
            {
                m_traj_preview = false;
                m_traj_is_playing = false;
            }
        }
        if ( ! m_traj_is_playing )
        {
            if ( m_view->m_timer->is_running() )
                m_view->m_timer->stop();
            m_traj_is_playing = false;
            m_traj_preview = false;
            if ( ! m_traj_is_paused  )
                m_traj_view_updates = 0;
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Edit") && !m_view->m_trajectory.empty() ) {
            if (ImGui::RadioButton("Trans", m_traj_guizmo_operation == ImGuizmo::TRANSLATE)) { m_traj_guizmo_operation = ImGuizmo::TRANSLATE; } ImGui::SameLine();
            if (ImGui::RadioButton("Rot", m_traj_guizmo_operation == ImGuizmo::ROTATE)) { m_traj_guizmo_operation = ImGuizmo::ROTATE; } ImGui::SameLine();

            if (ImGui::RadioButton("Local", m_traj_guizmo_mode == ImGuizmo::LOCAL)) { m_traj_guizmo_mode = ImGuizmo::LOCAL; } ImGui::SameLine();
            if (ImGui::RadioButton("World", m_traj_guizmo_mode == ImGuizmo::WORLD)) { m_traj_guizmo_mode = ImGuizmo::WORLD; }

            edit_trajectory(m_view->m_trajectory[m_selected_trajectory_idx]);
        }
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("CropBox")) {
        static float minX = -1, maxX = 1, minY = -1, maxY = 1, minZ = -1, maxZ = 1;
        static bool removeOutside = true;
        static bool showPlanes = false;
        ImGui::SliderFloat("min X", &minX, -1000.0, 1000.0);
        ImGui::SliderFloat("max X", &maxX, -1000.0, 1000.0);
        ImGui::SliderFloat("min Y", &minY, -1000.0, 1000.0);
        ImGui::SliderFloat("max Y", &maxY, -1000.0, 1000.0);
        ImGui::SliderFloat("min Z", &minZ, -1000.0, 1000.0);
        ImGui::SliderFloat("max Z", &maxZ, -1000.0, 1000.0);
        if ( minX > maxX ) { const float tmp = minX; minX = maxX; maxX = tmp; }
        if ( minY > maxY ) { const float tmp = minY; minY = maxY; maxY = tmp; }
        if ( minZ > maxZ ) { const float tmp = minZ; minZ = maxZ; maxZ = tmp; }
        static Eigen::Array<double,1,3> prevMinXYZ(0,0,0), prevMaxXYZ(1,1,1);
        const Eigen::Array<double,1,3> minXYZ(minX,minY,minZ), maxXYZ(maxX,maxY,maxZ);
        static MeshSharedPtr planes = std::make_shared<Mesh>();

        if (ImGui::RadioButton("removeOutside", removeOutside)) { removeOutside = true; }
        if (ImGui::RadioButton("removeInside", !removeOutside)) { removeOutside = false; }
        ImGui::Checkbox("ShowPlanes", &showPlanes);
        if ( showPlanes )
        {
            //VLOG(1) << "showing planes";
            if ( planes->V.rows() != 8 ) planes->V.resize(8,3);
            if ( planes->E.rows() != 12 ){
                planes->E.resize(12,2);
                planes->E.row(0) << 0,1;
                planes->E.row(1) << 1,2;
                planes->E.row(2) << 2,3;
                planes->E.row(3) << 3,0;

                planes->E.row(4) << 4,5;
                planes->E.row(5) << 5,6;
                planes->E.row(6) << 6,7;
                planes->E.row(7) << 7,4;

                planes->E.row(8) << 0,4;
                planes->E.row(9) << 1,5;
                planes->E.row(10) << 2,6;
                planes->E.row(11) << 3,7;
            }
            if ( planes->F.rows() != 12 ){
                planes->F.resize(12,3); planes->F.setZero();
                planes->F.row(0) << 0, 3, 2; // bottom
                planes->F.row(1) << 2, 1, 0;
                planes->F.row(2) << 4, 5, 6; // top
                planes->F.row(3) << 6, 7, 4;
                planes->F.row(4) << 0, 1, 5;
                planes->F.row(5) << 5, 4, 0;
                planes->F.row(6) << 1, 2, 6;
                planes->F.row(7) << 6, 5, 1;
                planes->F.row(8) << 2, 3, 7;
                planes->F.row(9) << 7, 6, 2;
                planes->F.row(10) << 3, 0, 4;
                planes->F.row(11) << 4, 7, 3;
            }
            if ( planes->C.rows() != 8 ){
                planes->C.resize(8,3);
                planes->C.row(0) << 1.0, 0.0, 0.0;
                planes->C.row(1) << 0.0, 1.0, 0.0;
                planes->C.row(2) << 0.0, 0.0, 1.0;
                planes->C.row(3) << 1.0, 1.0, 1.0;
                planes->C.row(4) << 1.0, 0.0, 0.0;
                planes->C.row(5) << 0.0, 1.0, 0.0;
                planes->C.row(6) << 0.0, 0.0, 1.0;
                planes->C.row(7) << 1.0, 1.0, 1.0;
            }

            if ( std::abs((minXYZ-prevMinXYZ).sum()) > 0 || std::abs((maxXYZ-prevMaxXYZ).sum()) > 0 )
            {
                VLOG(1) << "updating planes: " << minXYZ << " " << maxXYZ;
                planes->V.row(0) << minX, minY, minZ;
                planes->V.row(1) << minX, minY, maxZ;
                planes->V.row(2) << minX, maxY, maxZ;
                planes->V.row(3) << minX, maxY, minZ;
                planes->V.row(4) << maxX, minY, minZ;
                planes->V.row(5) << maxX, minY, maxZ;
                planes->V.row(6) << maxX, maxY, maxZ;
                planes->V.row(7) << maxX, maxY, minZ;
                planes->m_is_dirty = true;
                Scene::show(planes, "CropPlanes");
            }
            prevMinXYZ = minXYZ;
            prevMaxXYZ = maxXYZ;
        }
        if (ImGui::Button("Crop"))
        {
            // get mesh
            // apply crop
            if ( Scene::nr_meshes() > 0 )
            {
                MeshSharedPtr mesh=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx);
                MeshSharedPtr cropped =  std::make_shared<Mesh>(mesh->clone());

                int copy_idx = 0, numInside = 0;
                for ( int idx = 0; idx < mesh->V.rows(); ++idx)
                {
                    const Eigen::Vector3d pt = mesh->model_matrix() * Eigen::Vector3d(mesh->V.row(idx).transpose());
                    const bool insideTheBox = (minXYZ < pt.transpose().array()).all() && (pt.transpose().array() < maxXYZ).all();
                    numInside += insideTheBox;
                    if ( (removeOutside && insideTheBox) || (!removeOutside && !insideTheBox) )
                    {
                        cropped->V.row(copy_idx) = mesh->V.row(idx);
                        if ( mesh->C.rows() > 0 ) cropped->C.row(copy_idx) = mesh->C.row(idx);
                        if ( mesh->D.rows() > 0 ) cropped->D.row(copy_idx) = mesh->D.row(idx);
                        if ( mesh->NV.rows() > 0 ) cropped->NV.row(copy_idx) = mesh->NV.row(idx);
                        if ( mesh->L_pred.rows() > 0 ) cropped->L_pred.row(copy_idx)=mesh->L_pred.row(idx);
                        if ( mesh->L_gt.rows() > 0 ) cropped->L_gt.row(copy_idx)=mesh->L_gt.row(idx);
                        if ( mesh->I.rows() > 0 ) cropped->I.row(copy_idx) = mesh->I.row(idx);
                        ++copy_idx;
                        // TODO: do it the right way for all other fields.
                        //cloned.F=F;
                        //cloned.E=E;
                        //cloned.NF=NF;
                        //cloned.UV=UV;
                        //cloned.V_tangent_u=V_tangent_u;
                        //cloned.V_length_v=V_length_v;
                    }
                }
                cropped->V.conservativeResize(copy_idx,cropped->V.cols());
                if ( mesh->C.rows() > 0 ) cropped->C.conservativeResize(copy_idx,cropped->C.cols());
                if ( mesh->D.rows() > 0 ) cropped->D.conservativeResize(copy_idx,cropped->D.cols());
                if ( mesh->NV.rows() > 0 ) cropped->NV.conservativeResize(copy_idx,cropped->NV.cols());
                if ( mesh->L_pred.rows() > 0 ) cropped->L_pred.conservativeResize(copy_idx,cropped->L_pred.cols());
                if ( mesh->L_gt.rows() > 0 ) cropped->L_gt.conservativeResize(copy_idx,cropped->L_gt.cols());
                if ( mesh->I.rows() > 0 ) cropped->I.conservativeResize(copy_idx,cropped->I.cols());
                cropped->F.resize(0,0);
                cropped->E.resize(0,0);
                std::string cropped_name=mesh->name+"_cropped";
                VLOG(1) << "cropped with name "<<cropped_name << " idx: " << copy_idx << " inside: " << numInside;
                Scene::show(cropped, cropped_name);
            }
        }
    }



    ImGui::Separator();
    if (ImGui::CollapsingHeader("Lights")) {
        if(ImGui::ListBoxHeader("Selected lights", m_view->m_spot_lights.size(), 6)){ //all the spot lights

            //push light selection box for the spot lights
            for (int i = 0; i < (int)m_view->m_spot_lights.size(); i++) {
                if( m_selected_spot_light_idx == i ){
                    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }
                if(ImGui::Selectable( ("SpotLight_"+std::to_string(i)).c_str(), true ) ){
                    m_selected_spot_light_idx=i;
                }
                ImGui::PopStyleColor(1);
            }
            ImGui::ListBoxFooter();

            //modify properties
            ImGui::SliderFloat("Power", &m_view->m_spot_lights[m_selected_spot_light_idx]->m_power, 100.0, 500.0);
            ImGui::ColorEdit3("Color", m_view->m_spot_lights[m_selected_spot_light_idx]->m_color.data());


        }

    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("IO")) {
        ImGui::InputText("write_mesh_path", m_write_mesh_file_path);
        if (ImGui::Button("Write mesh")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->save_to_file(m_write_mesh_file_path);
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Recorder")) {
        ImGui::InputText("record_path", m_view->m_recording_path);
        ImGui::InputText("snapshot_name", m_view->m_snapshot_name);
        if(ImGui::Button("Write viewer to png") ){
            if(m_view->m_record_gui){
                m_view->m_recorder->write_without_buffering(m_view->m_final_fbo_with_gui.tex_with_name("color_gtex"), m_view->m_snapshot_name, m_view->m_recording_path);
            }else{
                if (m_view->m_record_with_transparency){
                    m_view->m_recorder->write_without_buffering(m_view->m_final_fbo_no_gui.tex_with_name("color_with_transparency_gtex"), m_view->m_snapshot_name, m_view->m_recording_path);
                }else{
                    m_view->m_recorder->write_without_buffering(m_view->m_final_fbo_no_gui.tex_with_name("color_without_transparency_gtex"), m_view->m_snapshot_name, m_view->m_recording_path);
                }
            }
        }
        ImGui::Checkbox("Record GUI", &m_view->m_record_gui);
        ImGui::Checkbox("Record with transparency", &m_view->m_record_with_transparency);
        // ImGui::SliderFloat("Magnification", &m_view->m_recorder->m_magnification, 1.0f, 5.0f);

        //recording
        ImVec2 button_size(25*m_hidpi_scaling,25*m_hidpi_scaling);
        const char* icon_recording = m_view->m_recorder->is_recording() ? ICON_FA_PAUSE : ICON_FA_CIRCLE;
        if(ImGui::Button(icon_recording, button_size) ){
            if(m_view->m_recorder->is_recording()){
               m_view->m_recorder->stop_recording();
            }else{
               m_view->m_recorder->start_recording();
            }
        }
        ImGui::SameLine();
        if ( ImGui::Button("Pause") )
        {
            m_view->m_recorder->pause_recording();
        }
        if(ImGui::Button("Record orbit") ){
            m_view->m_recorder->record_orbit( m_view->m_recording_path );
        }
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Profiler")) {
        ImGui::Checkbox("Profile gpu", &Profiler_ns::m_profile_gpu);
        ImGui::SameLine(); help_marker("The profiler by default measures time spent in CPU functions. Enabling GPU profiling causes calls to OpenGL to be blocking and therefore the profiler will now also measure the time spent in GPU functions. Enabling GPU profiling slows down the whole application.");
        if (ImGui::Button("Print profiling stats")){
            Profiler_ns::Profiler::print_all_stats();
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Show debug textures", &m_show_debug_textures);
        // if (ImGui::Curve("Das editor", ImVec2(600, 200), 10, m_curve_points)){
            // curve changed
        // }
        if (ImGui::Button("Write gbuffer to folder")){
            m_view->write_gbuffer_to_folder();
        }
    }
    if(m_show_debug_textures){
        show_gl_texture(m_view->m_gbuffer.tex_with_name("diffuse_gtex").tex_id(), "diffuse_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("normal_gtex").tex_id(), "normal_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("depth_gtex").tex_id(), "depth_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("metalness_and_roughness_gtex").tex_id(), "metalness_and_roughness_gtex", true);
        if (m_view->m_render_uv_to_gbuffer){
            show_gl_texture(m_view->m_gbuffer.tex_with_name("uv_gtex").tex_id(), "uv_gtex", true);
        }
        show_gl_texture(m_view->m_depth_linear_tex.tex_id(), "depth_linear_tex", true);
        show_gl_texture(m_view->m_ao_tex.tex_id(), "ao_tex", true);
        show_gl_texture(m_view->m_ao_blurred_tex.tex_id(), "ao_blurred_tex", true);
        show_gl_texture(m_view->m_brdf_lut_tex.tex_id(), "brdf_lut_tex", true);
        // show_gl_texture(m_view->m_composed_tex.tex_id(), "composed_tex", true);
        show_gl_texture(m_view->m_composed_fbo.tex_with_name("composed_gtex").tex_id(), "composed_gtex", true);
        show_gl_texture(m_view->m_composed_fbo.tex_with_name("bloom_gtex").tex_id(), "bloom_gtex", true);
        // show_gl_texture(m_view->m_posprocessed_tex.tex_id(), "posprocessed_tex", true);
        show_gl_texture(m_view->m_blur_tmp_tex.tex_id(), "blur_tmp_tex", true);
        show_gl_texture(m_view->m_final_fbo_no_gui.tex_with_name("color_without_transparency_gtex").tex_id(), "fbo_no_transparency_no_gui", true);
        show_gl_texture(m_view->m_final_fbo_no_gui.tex_with_name("color_without_transparency_gtex").tex_id(), "fbo_with_transparency_no_gui", true);
        show_gl_texture(m_view->m_final_fbo_with_gui.tex_with_name("color_gtex").tex_id(), "fbo_with_gui", true);
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Misc")) {
        ImGui::SliderInt("log_level", &loguru::g_stderr_verbosity, -3, 9);
    }


    ImGui::Separator();
    ImGui::TextUnformatted(("Nr of points: " + format_with_commas(Scene::nr_vertices())).data());
    ImGui::TextUnformatted(("Nr of triangles: " + format_with_commas(Scene::nr_vertices())).data());
    ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);



    if (ImGui::Button("Test Window")) m_show_demo_window ^= 1;
    if (ImGui::Button("Profiler Window")) m_show_profiler_window ^= 1;
    if (ImGui::Button("Player Window")) m_show_player_window ^= 1;



    // if (ImGui::Curve("Das editor", ImVec2(400, 200), 10, foo))
    // {
    //   // foo[0].y=ImGui::CurveValue(foo[0].x, 5, foo);
    //   // foo[1].y=ImGui::CurveValue(foo[1].x, 5, foo);
    //   // foo[2].y=ImGui::CurveValue(foo[2].x, 5, foo);
    //   // foo[3].y=ImGui::CurveValue(foo[3].x, 5, foo);
    //   // foo[4].y=ImGui::CurveValue(foo[4].x, 5, foo);
    // }

    ImGui::PopItemWidth();

    ImGui::End();
}

void Gui::draw_profiler(){

    ImVec2 canvas_size = ImGui::GetIO().DisplaySize;

   if (m_show_profiler_window && Profiler_ns::m_timings.size()>0 ){
        int nr_timings=Profiler_ns::m_timings.size();
        ImVec2 size(330*m_hidpi_scaling,50*m_hidpi_scaling*nr_timings);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(canvas_size.x -size.x , 0));
        ImGui::Begin("Profiler", nullptr,
            ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            // | ImGuiWindowFlags_NoScrollbar
            // | ImGuiWindowFlags_NoScrollWithMouse
            // | ImGuiWindowFlags_NoCollapse
            // | ImGuiWindowFlags_NoSavedSettings
            // | ImGuiWindowFlags_NoInputs
            );
        ImGui::PushItemWidth(100*m_hidpi_scaling);


        for (size_t i = 0; i < Profiler_ns::m_ordered_timers.size(); ++i){
            const std::string name = Profiler_ns::m_ordered_timers[i];
            auto stats=Profiler_ns::m_stats[name];
            auto times=Profiler_ns::m_timings[name];

            std::stringstream stream_exp_mean;
            std::stringstream stream_mean;
            // stream_cma << std::fixed <<  std::setprecision(1) << stats.exp_mean;
            stream_exp_mean << std::fixed <<  std::setprecision(1) << stats.exp_mean;
            stream_mean << std::fixed <<  std::setprecision(1) << stats.mean;
            // std::string s_cma = stream_cma.str();

    //    std::string title = times.first +  "\n" + "(" + s_exp + ")" + "(" + s_cma + ")";
            std::string title = name +  "\n" + "exp_avg: " + stream_exp_mean.str() + " ms " + "(avg:"+stream_mean.str()+")";
            // std::string title = name +  "\n" + "exp_avg: " + stream_exp_mean.str() + " ms " + "("+stream_mean.str()+")";
            ImGui::PlotLines(title.data(), times.data() , times.size() ,times.get_front_idx() );
        }
        ImGui::PopItemWidth();
        ImGui::End();
    }
}

void Gui::show(const cv::Mat cv_mat_0, const std::string name_0, 
               const cv::Mat cv_mat_1, const std::string name_1,
               const cv::Mat cv_mat_2, const std::string name_2){

    if(!cv_mat_0.data){
        VLOG(3) << "Showing empty image, discaring with name "<< name_0;
        return;
    }

    //see how many images we have in this window
    int imgs_in_window=1; //start at 1 because name_0 surely exists here
    if (!name_1.empty()) imgs_in_window++;
    if (!name_2.empty()) imgs_in_window++;

    std::lock_guard<std::mutex> lock(m_cv_mats_mutex);  // so that "show" can be usef from any thread

    // m_cv_mats_map[name] = cv_mat.clone(); //TODO we shouldnt clone on top of this one because it might be at the moment used for transfering between cpu and gpu
    // m_cv_mats_map[name_0] = cv_mat_0; //TODO we shouldnt clone on top of this one because it might be at the moment used for transfering between cpu and gpu
    // m_cv_mats_dirty_map[name_0]=true;


    //check if the window exists
    std::string window_name=name_0;
    if (imgs_in_window>1){
        if (!name_1.empty()) window_name+="_"+name_1;
        if (!name_2.empty()) window_name+="_"+name_2;
        window_name+="_flip";
    }
    auto got= m_win_imgs_map.find(window_name);
    if(got==m_win_imgs_map.end() ){
        //does not exists so we create it

        //make the window
        WindowImg window;
        window.named_imgs_vec.resize(imgs_in_window);
        //add the new window
        m_win_imgs_map.emplace(window_name, std::move(window));

        //first time we create te window we set the first one as selected
        // if (!name_1.empty()){
        // m_win_imgs_map[window_name].named_imgs_vec[1].change_selection_to_this=true;
        // }
    }

    //make the named imgs
    m_win_imgs_map[window_name].named_imgs_vec[0].is_dirty=true;
    m_win_imgs_map[window_name].named_imgs_vec[0].mat=cv_mat_0;
    m_win_imgs_map[window_name].named_imgs_vec[0].name=name_0;

    if (!name_1.empty()){
        m_win_imgs_map[window_name].named_imgs_vec[1].is_dirty=true;
        m_win_imgs_map[window_name].named_imgs_vec[1].mat=cv_mat_1;
        m_win_imgs_map[window_name].named_imgs_vec[1].name=name_1;
    }
    if (!name_2.empty()){
        m_win_imgs_map[window_name].named_imgs_vec[2].is_dirty=true;
        m_win_imgs_map[window_name].named_imgs_vec[2].mat=cv_mat_2;
        m_win_imgs_map[window_name].named_imgs_vec[2].name=name_2;
    }



}

void Gui::show_images(){

    std::lock_guard<std::mutex> lock(m_cv_mats_mutex);  // so that "show" can be used at the same time as the viewer thread shows images

    // //TODO check if the cv mats actually changed, maybe a is_dirty flag
    // for (auto const& x : m_cv_mats_map){
    //     std::string name=x.first;

    //     //check if it's dirty, if the cv mat changed since last time we displayed it
    //     if(m_cv_mats_dirty_map[name]){
    //         m_cv_mats_dirty_map[name]=false;
    //         //check if there is already a texture with the same name
    //         auto got= m_textures_map.find(name);
    //         if(got==m_textures_map.end() ){
    //             //the first time we shot this texture so we add it to the map otherwise there is already a texture there so we just update it
    //             m_textures_map.emplace(name, (name) ); //using inplace constructor of the Texture2D so we don't use a move constructor or something similar
    //         }
    //         //upload to this texture, either newly created or not
    //         gl::Texture2D& tex= m_textures_map[name];
    //         tex.upload_from_cv_mat(x.second);
    //     }


    //     gl::Texture2D& tex= m_textures_map[name];
    //     show_gl_texture(tex.tex_id(), name);


    // }


    //TODO check if the cv mats actually changed, maybe a is_dirty flag
    for (auto &win : m_win_imgs_map){
        std::string name=win.first;
        WindowImg& window=win.second;

        int nr_imgs_in_window=win.second.named_imgs_vec.size();

        //check if it's dirty, if the cv mat changed since last time we displayed it
        for(int i=0; i<nr_imgs_in_window; i++){
            if(win.second.named_imgs_vec[i].is_dirty ){
                win.second.named_imgs_vec[i].is_dirty;
                gl::Texture2D& tex= win.second.named_imgs_vec[i].tex;
                tex.upload_from_cv_mat( win.second.named_imgs_vec[i].mat );
            }
        }



        bool make_tabs=nr_imgs_in_window!=1;

        ImGui::SetNextWindowPos(ImVec2(400,400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(512,512), ImGuiCond_FirstUseEver);

        
        ImGuiWindowFlags window_flags = 0;
        window_flags |= ImGuiWindowFlags_MenuBar;
        ImGui::Begin(name.c_str(), nullptr, window_flags);



        //menu
        if (ImGui::BeginMenuBar()){
            // if (ImGui::BeginMenu("File")){
                // ImGui::EndMenu();
            // }
            if (ImGui::Button("Save")){
                // VLOG(1) << "save";
                int idx_selected= window.selected_img_idx();
                cv::Mat mat= window.named_imgs_vec[idx_selected].mat;
                fs::path root_path="./recordings/imgs/";
                if (!fs::exists(root_path)){
                    fs::create_directories(root_path);
                }
                std::string path= root_path.string()+window.named_imgs_vec[idx_selected].name +".png";
                cv::imwrite(path, mat);
                VLOG(1) << "Saved img to " << path;
            }
            // if (ImGui::Button("FixAspect")){ // Not sure how to do it properly because we need to resize the window by only having knowlege of the image part of it
            //     int idx_selected= window.selected_img_idx();
            //     cv::Mat mat= window.named_imgs_vec[idx_selected].mat;
            //     float aspect_ratio= (float)mat.cols /mat.rows;
            //     ImVec2 window_size=ImGui::GetWindowSize(); //not really the best way. Ideally we would get the size of only the image par
            //     // ImVec2 window_size = ImVec2( window.img_width, window.img_height );
            //     ImVec2 new_window_size=ImVec2( window_size.x,  window_size.x/aspect_ratio );
            //     ImGui::SetWindowSize(new_window_size);
            // }
            ImGui::EndMenuBar();
        }




        //if we hover and push the right and left arrows we can change the image
        if (ImGui::IsWindowHovered()){
            //get the idx of the img that was selected
            int idx_selected= window.selected_img_idx();
            int idx_left=radu::utils::wrap( idx_selected-1, nr_imgs_in_window );
            int idx_right=radu::utils::wrap( idx_selected+1, nr_imgs_in_window );

            ImGui::SetItemUsingMouseWheel();
            float wheel = ImGui::GetIO().MouseWheel;
            // VLOG(1) << "wheel"<< wheel;


            //switch to the new image 
            // ImGuiIO& io = ImGui::GetIO();
            if ( ImGui::IsKeyPressed( ImGui::GetKeyIndex(ImGuiKey_LeftArrow) )  ||  wheel<0  ){
                win.second.named_imgs_vec[idx_left].change_selection_to_this=true;
            }
            if ( ImGui::IsKeyPressed( ImGui::GetKeyIndex(ImGuiKey_RightArrow) )  ||  wheel>0  ){
                win.second.named_imgs_vec[idx_right].change_selection_to_this=true;
            }



        }



        //here we make our dockable things
        ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
        bool tab_ret= make_tabs? ImGui::BeginTabBar("MyTabBar", tab_bar_flags) : true;  //make the tabs only if we have more then 1 img
        if (tab_ret){
            for(int i=0; i<nr_imgs_in_window; i++){
                NamedImg& cur_img = win.second.named_imgs_vec[i];

                ImGuiTabItemFlags tab_flags=ImGuiTabItemFlags_None;
                if (cur_img.change_selection_to_this) { tab_flags|=ImGuiTabItemFlags_SetSelected; }
                cur_img.change_selection_to_this=false;
                cur_img.is_selected=false;



                // if (make_tabs && ImGui::BeginTabItem(cur_img.name.c_str(), nullptr,  tab_flags  )){
                bool item_ret= make_tabs? ImGui::BeginTabItem(cur_img.name.c_str(), nullptr,  tab_flags  ) : true; 
                if(item_ret){
                    cur_img.is_selected=true;

                    gl::Texture2D& tex= window.named_imgs_vec[i].tex;
                    // ImVec2 vMin=ImGui::GetItemRectMin(); //get the position before and fter submitting the image so we know it's bounds https://discourse.dearimgui.org/t/how-to-know-if-a-tab-bar-of-a-docked-window-is-hidden-and-its-height/316/5
                    ImVec2 vMin= ImGui::GetCursorScreenPos();
                    if (!cur_img.is_cropped){
                        ImGui::Image((ImTextureID)(uintptr_t) tex.tex_id() , ImGui::GetContentRegionAvail() );
                    }else{
                        ImGui::Image((ImTextureID)(uintptr_t) tex.tex_id() , ImGui::GetContentRegionAvail(),  
                        ImVec2( cur_img.crop_start_uv.x(), cur_img.crop_start_uv.y()  ) , ImVec2( cur_img.crop_end_uv.x(), cur_img.crop_end_uv.y()  ) );
                    }
                    const bool is_img_hovered = ImGui::IsItemHovered(); // Hovered
                    ImVec2 vMax=ImGui::GetItemRectMax();
                    // ImVec2 vMax=ImGui::GetCursorScreenPos();
                    // ImGui::GetForegroundDrawList()->AddRect( vMin, vMax, IM_COL32( 255, 255, 0, 255 ) ); //debug
                    int width_img=vMax.x - vMin.x;
                    int height_img=vMax.y - vMin.y;
                    window.img_width=width_img;
                    window.img_height=height_img;

                    //crop
                    if (is_img_hovered){
                        ImVec2 mouse_pos=ImGui::GetMousePos(); //in screen coordinats 0,0 is at the top left of your screen
                        ImVec2 pos_wrt_img=mouse_pos-vMin;
                        // VLOG(1) <<" pos_wrt_img " << pos_wrt_img.x << " " << pos_wrt_img.y;
                        ImVec2 uv_wrt_img;
                        uv_wrt_img.x =pos_wrt_img.x/width_img;
                        uv_wrt_img.y =pos_wrt_img.y/height_img;
                        // VLOG(1) <<"uv_wrt_img" << uv_wrt_img.x << " " << uv_wrt_img.y;

                        //drag and create a rectangle in screen coordinate and one in uv coordinates
                        //similar to the imgui_demo Canvas
                        if (!cur_img.is_cropped){
                            if (!cur_img.is_cropping && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
                                cur_img.crop_start_uv << uv_wrt_img.x,  uv_wrt_img.y;
                                cur_img.crop_end_uv << uv_wrt_img.x,  uv_wrt_img.y;
                                cur_img.screen_pos_start << mouse_pos.x, mouse_pos.y;
                                cur_img.is_cropping = true;
                            }
                            if (cur_img.is_cropping){
                                cur_img.crop_end_uv << uv_wrt_img.x,  uv_wrt_img.y;
                                cur_img.screen_pos_end << mouse_pos.x, mouse_pos.y;
                                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
                                    bool is_crop_big_enough=false; //sometimes we just click on the image and we don't actually want to crop anything
                                    float crop_size= (cur_img.crop_start_uv - cur_img.crop_end_uv).norm();
                                    // VLOG(1) << "crop size" << crop_size;
                                    if (crop_size>0.01){
                                        is_crop_big_enough=true;
                                    }


                                    cur_img.is_cropping = false;
                                    cur_img.is_cropped = is_crop_big_enough;
                                    //now that we finished cropping this img, copy this crop to all the other imgs in the window
                                    for(int j=0; j<nr_imgs_in_window; j++){
                                        NamedImg& other_img = win.second.named_imgs_vec[j];
                                        other_img.crop_start_uv=cur_img.crop_start_uv;
                                        other_img.crop_end_uv=cur_img.crop_end_uv;
                                        other_img.screen_pos_start=cur_img.screen_pos_start;
                                        other_img.screen_pos_end=cur_img.screen_pos_end;
                                        other_img.is_cropping=false;
                                        other_img.is_cropped=is_crop_big_enough;
                                    }
                                }
                                //draw rectangle
                                ImGui::GetForegroundDrawList()->AddRect( ImVec2(cur_img.screen_pos_start.x(), cur_img.screen_pos_start.y()), ImVec2(cur_img.screen_pos_end.x(), cur_img.screen_pos_end.y()), 
                                IM_COL32( 255, 255, 0, 255 ) );
                            }
                        }
                        if (ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
                            cur_img.is_cropping = false;
                            cur_img.is_cropped = false;
                            //uncrop the rest of the img in the window
                            for(int j=0; j<nr_imgs_in_window; j++){
                                NamedImg& other_img = win.second.named_imgs_vec[j];
                                other_img.is_cropped=false;
                            }
                        }



                    }

    
                    if(make_tabs) ImGui::EndTabItem();
                }
            
            }
            if(make_tabs) ImGui::EndTabBar();
        }

        

        ImGui::End(); //finish window

        // }




    }



    
}



void Gui::show_gl_texture(const int tex_id, const std::string window_name, const bool flip){
    //show camera left
    if(tex_id==-1){
        return;
    }
    ImGuiWindowFlags window_flags = 0;
    ImGui::SetNextWindowPos(ImVec2(400,400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(512,512), ImGuiCond_FirstUseEver);
    ImGui::Begin(window_name.c_str(), nullptr, window_flags);
    if(flip){ //framebuffer in gpu are stored upside down  for some reson
        ImGui::Image((ImTextureID)(uintptr_t)tex_id, ImGui::GetContentRegionAvail(), ImVec2(0,1), ImVec2(1,0) );  //the double cast is needed to avoid compiler warning for casting int to void  https://stackoverflow.com/a/30106751
    }else{
        ImGui::Image((ImTextureID)(uintptr_t)tex_id, ImGui::GetContentRegionAvail());
    }
    ImGui::End();
}

//similar to libigl draw_text https://github.com/libigl/libigl/blob/master/include/igl/opengl/glfw/imgui/ImGuiMenu.cpp
void Gui::draw_overlays(){

    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    bool visible = true;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("ViewerLabels", &visible,
        ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoInputs);

    for (int i = 0; i < Scene::nr_meshes(); i++) {
        MeshSharedPtr mesh=m_view->m_scene->get_mesh_with_idx(i);
        //draw vert ids
        if(mesh->m_vis.m_is_visible && mesh->m_vis.m_show_vert_ids){
            for (int i = 0; i < mesh->V.rows(); ++i){
                draw_overlay_text( mesh->V.row(i), mesh->model_matrix().cast<float>().matrix(), std::to_string(i), mesh->m_vis.m_label_color );
            }
        }
        //draw vert coords in x,y,z format
        if(mesh->m_vis.m_is_visible && mesh->m_vis.m_show_vert_coords){
            for (int i = 0; i < mesh->V.rows(); ++i){
                // std::string coord_string = "(" + std::to_string(mesh->V(i,0)) + "," + std::to_string(mesh->V(i,1)) + "," + std::to_string(mesh->V(i,2)) + ")";
                std::stringstream stream;
                stream << std::fixed << std::setprecision(3) << "(" << mesh->V(i,0) << ", " << mesh->V(i,1) << ", " << mesh->V(i,2) << ")";
                std::string coord_string = stream.str();
                draw_overlay_text( mesh->V.row(i), mesh->model_matrix().cast<float>().matrix(), coord_string, mesh->m_vis.m_label_color );
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();


}

//similar to libigl draw_text https://github.com/libigl/libigl/blob/master/include/igl/opengl/glfw/imgui/ImGuiMenu.cpp
void Gui::draw_overlay_text(const Eigen::Vector3d pos, const Eigen::Matrix4f model_matrix, const std::string text, const Eigen::Vector3f color){
    // std::cout <<"what" << std::endl;
    Eigen::Vector4f pos_4f;
    pos_4f << pos.cast<float>(), 1.0;

    Eigen::Matrix4f M = model_matrix;
    Eigen::Matrix4f V = m_view->m_camera->view_matrix();
    Eigen::Matrix4f P = m_view->m_camera->proj_matrix(m_view->m_viewport_size);
    Eigen::Matrix4f MVP = P*V*M;

    pos_4f= MVP * pos_4f;

    pos_4f = pos_4f.array() / pos_4f(3);
    pos_4f = pos_4f.array() * 0.5f + 0.5f;
    pos_4f(0) = pos_4f(0) * m_view->m_viewport_size(0);
    pos_4f(1) = pos_4f(1) * m_view->m_viewport_size(1);

    Eigen::Vector3f pos_screen= pos_4f.head(3);

    // Draw text labels slightly bigger than normal text
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2,
        // ImVec2(pos_screen[0] , ( - pos_screen[1])),
        ImVec2(pos_screen[0], (m_view->m_viewport_size(1) - pos_screen[1]) ),
        ImGui::GetColorU32(ImVec4(
            color(0),
            color(1),
            color(2),
            1.0)),
    &text[0], &text[0] + text.size());
}

void Gui::draw_label_mngr_legend(){
    //get the selected mesh
    //for the selected mesh we show the label mngr labels and color

    if(Scene::nr_meshes()!=0){
        MeshSharedPtr mesh=Scene::get_mesh_with_idx(m_selected_mesh_idx);
        if ( mesh->m_label_mngr){

            ImVec2 canvas_size = ImGui::GetIO().DisplaySize;

            // ImGuiWindowFlags window_flags = 0;
            // ImVec2 size=ImVec2(canvas_size.x*0.5-260, canvas_size.y*0.11);
            ImVec2 size=ImVec2(canvas_size.x*0.5-210, canvas_size.y*0.11);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(canvas_size.x/2-size.x/2, canvas_size.y-size.y));
            ImGui::Begin("LabelMngr", nullptr,
                ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse
                | ImGuiWindowFlags_NoCollapse
                // | ImGuiWindowFlags_NoSavedSettings
                // | ImGuiWindowFlags_NoInputs
                );

            //since the labelmngr stores the colors in a row major way, the colors of a certain label (a row in the matrix) are not contiguous and therefore cannot be edited with coloredit3. So we copy the color into a small vec3
            int nr_drawn_labels=0;
            for(int i=0; i<mesh->m_label_mngr->nr_classes(); i++){
                std::string label=mesh->m_label_mngr->idx2label(i);
                // if(i==mesh->m_label_mngr->get_idx_unlabeled()){
                    // continue; //don't shot the background label
                // }
                // if(i>mesh->m_label_mngr->nr_classes()-1){
                    // continue;
                // }
                if (label=="other-ground"){
                    continue;
                }
                Eigen::Vector3f color=Eigen::Vector3d(mesh->m_label_mngr->color_scheme().row(i)).cast<float>();
                float widget_size=20*m_hidpi_scaling;
                ImGui::SetNextItemWidth(widget_size); //only gives sizes to the widget and not the label
                if( ImGui::ColorEdit3(label.c_str(), color.data(), ImGuiColorEditFlags_NoInputs) ){
                    mesh->m_label_mngr->set_color_for_label_with_idx(i, color.cast<double>());
                }
                nr_drawn_labels++;
                float x_size_label=ImGui::CalcTextSize(label.c_str()).x;
                float total_size=widget_size+x_size_label; //total size of the color rectangle widget+label
                if(nr_drawn_labels%6!=0 || nr_drawn_labels==0){
                    ImGui::SameLine();
                    ImGui::Dummy(ImVec2(218.0f-total_size, 0.0f));
                    ImGui::SameLine();
                }

            }
        }
        // ImGui::ColorEdit3("Color", m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_solid_color.data(), ImGuiColorEditFlags_NoInputs);

        ImGui::End();
    }


}

void Gui::draw_drag_drop_text(){

    if (Scene::nr_meshes()==0){
        ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        bool visible = true;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        // ImGui::PushFont(m_dragdrop_font);
        ImGui::Begin("DragDrop", &visible,
            ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoInputs);


        // Draw text slightly bigger than normal text
        std::string text= "Drag and drop mesh file to display it";
        // Eigen::Vector3f color;
        // color << 1.0, 1.0, 1.0;
        // ImDrawList* drawList = ImGui::GetWindowDrawList();
        // drawList->AddText(NULL, 30.0,
        //     // ImVec2(m_view->m_viewport_size(0)/2*m_hidpi_scaling , (m_view->m_viewport_size(1) - m_view->m_viewport_size(1)/2 ) *m_hidpi_scaling ),
        //     ImVec2(m_view->m_viewport_size(0)/2 - ImGui::CalcTextSize(text.c_str()).x/2 ,  m_view->m_viewport_size(1)/2  ),
        //     ImGui::GetColorU32(ImVec4(
        //         color(0),
        //         color(1),
        //         color(2),
        //         1.0)),
        // &text[0], &text[0] + text.size());

        //attempt 2
        float font_size = ImGui::GetFontSize() * text.size() / 2;
        ImGui::SameLine(
        ImGui::GetWindowSize().x / 2 -
        font_size + (font_size / 2)
        );
        ImGui::TextUnformatted(text.c_str());




        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        // ImGui::PopFont();
    }

}

void Gui::edit_transform(const MeshSharedPtr& mesh){


    Eigen::Matrix4f widget_placement;
    widget_placement.setIdentity();
    Eigen::Matrix4f view = m_view->m_camera->view_matrix();
    Eigen::Matrix4f proj = m_view->m_camera->proj_matrix(m_view->m_viewport_size);

    ImGuizmo::BeginFrame();
    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    Eigen::Matrix4f delta;
    delta.setIdentity();

    Eigen::Affine3d model_matrix=mesh->model_matrix();
    ImGuizmo::Manipulate(view.data(), proj.data(), m_guizmo_operation, m_guizmo_mode, model_matrix.cast<float>().data(), delta.data() );
    if(m_guizmo_operation==ImGuizmo::SCALE){
        delta=Eigen::Matrix4f::Identity()-(Eigen::Matrix4f::Identity()-delta)*0.1; //scaling is for some reason very fast, make it a bit slower
    }



    //update the model matrix with the delta and updates the model matrix of all the children
    if( (delta-Eigen::Matrix4f::Identity() ).norm()>1e-9){ //if the movement is very small then we don't need to do anything and therefore we save ourselves from doing one update to the shadow map
        mesh->transform_model_matrix(Eigen::Affine3d(delta.cast<double>()));
    }
    // Eigen::Matrix4f new_model_matrix;
    // new_model_matrix=mesh->m_model_matrix.matrix().cast<float>();
    // new_model_matrix=delta*mesh->m_model_matrix.cast<float>().matrix();
    // mesh->m_model_matrix=Eigen::Affine3d(new_model_matrix.cast<double>());

    //update all children
    // VLOG(1) << "updating children " << mesh->m_child_meshes.size();
    // for (size_t i = 0; i < mesh->m_child_meshes.size(); i++){
    //     MeshSharedPtr child=mesh->m_child_meshes[i];

    //     Eigen::Matrix4f new_model_matrix;
    //     new_model_matrix=child->m_model_matrix.matrix().cast<float>();
    //     new_model_matrix=delta*child->m_model_matrix.cast<float>().matrix();
    //     child->m_model_matrix=Eigen::Affine3d(new_model_matrix.cast<double>());
    // }

    Eigen::Matrix3d rot = mesh->model_matrix().linear();
    Eigen::Quaterniond q(rot);
    // VLOG(1) << "Model matrix is " << mesh->m_model_matrix.translation().x() << ", " << mesh->m_model_matrix.translation().y() << ", " << mesh->m_model_matrix.translation().z() << " quat(x,y,z,w) is: " << q.x() << ", " << q.y() << ", " << q.z() << ", " << q.w();

}

void Gui::edit_trajectory(const std::shared_ptr<Camera> & cam){

    Eigen::Matrix4f widget_placement;
    widget_placement.setIdentity();
    Eigen::Matrix4f view = m_view->m_camera->view_matrix();
    Eigen::Matrix4f proj = m_view->m_camera->proj_matrix(m_view->m_viewport_size);

    ImGuizmo::BeginFrame();
    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    Eigen::Matrix4f delta;
    delta.setIdentity();

    ImGuizmo::Manipulate(view.data(), proj.data(), m_traj_guizmo_operation, m_traj_guizmo_mode, cam->m_model_matrix.data(), delta.data() );

    //update the model matrix with the delta and updates the model matrix of all the children
    cam->transform_model_matrix(Eigen::Affine3f(delta));
}

void Gui::draw_trajectory( const std::string & trajectory_mesh_name, const std::string & frustum_mesh_name )
{
    if ( m_view->m_trajectory.empty() ) return;
    MeshSharedPtr trajectory_mesh = Mesh::create();
    MeshSharedPtr frustum_mesh = Mesh::create();
    std::shared_ptr<Camera> prevCam = nullptr;
    for ( int i=0; i< int(m_view->m_trajectory.size()); ++i )
    {
        std::shared_ptr<Camera> cam = m_view->m_trajectory[i];
        if ( ! cam ) continue;

        if ( prevCam && cam->m_traj.m_enabled )
        {
            // interpolation:
            const Eigen::Affine3d prevPose = prevCam->m_model_matrix.cast<double>();
            const Eigen::Affine3d curPose = cam->m_model_matrix.cast<double>();
            Eigen::Affine3d diffPose = prevPose.inverse() * curPose;
            const double scaleDiff = diffPose.translation().norm();
            diffPose.translation() /= scaleDiff;
            const Eigen::Matrix<double,6,1> omega = SophusLog(diffPose);
            Eigen::Affine3d lastPose = prevPose;
            std::vector<Eigen::VectorXd> interpV, interpC;
            std::vector<Eigen::VectorXi> interpE;
            interpV.emplace_back(prevPose.translation());
            interpC.emplace_back(Eigen::Vector3d(1,0,0));
            const double dt = .05;
            for ( float t = dt; t < 1.f; t+=dt)
            {
                // incremental drawing
                Eigen::Affine3d dx = SophusExp(t * omega);
                dx.translation() *= scaleDiff;
                const double newT = dt / (1-t+dt);
                const Eigen::Affine3d newPose = interpolateSE3(lastPose,curPose,newT);
                lastPose = newPose;
                interpV.emplace_back(newPose.translation());
                interpC.emplace_back(Eigen::Vector3d::Ones());
                interpE.emplace_back((Eigen::Vector2i() << interpV.size()-2,interpV.size()-1).finished());
            }
            interpV.emplace_back(curPose.translation());
            interpC.emplace_back(Eigen::Vector3d(0,1,0));
            interpE.emplace_back((Eigen::Vector2i() << interpV.size()-2,interpV.size()-1).finished());

            MeshSharedPtr interpolatedMesh = Mesh::create();
            interpolatedMesh->V = vec2eigen(interpV);
            interpolatedMesh->C = vec2eigen(interpC);
            interpolatedMesh->E = vec2eigen(interpE);
            trajectory_mesh->add(*interpolatedMesh);
        }

        MeshSharedPtr camMesh = cam->create_frustum_mesh( m_trajectory_frustum_size, m_view->m_viewport_size);
        camMesh->apply_model_matrix_to_cpu(true);
        if ( i != m_selected_trajectory_idx )
            camMesh->C *= 0.75;
        if ( !cam->m_traj.m_enabled )
            camMesh->C.setConstant(0.25);
        else
            prevCam = cam;
        frustum_mesh->add(*camMesh);
    }
    if ( trajectory_mesh->V.rows() > 0 ){
        trajectory_mesh->m_vis.m_point_size = 20;
        trajectory_mesh->m_vis.set_color_pervertcolor();
        trajectory_mesh->m_vis.m_show_points=true;
        trajectory_mesh->m_vis.m_show_mesh=false;
        trajectory_mesh->m_vis.m_show_lines=true;
        Scene::show(trajectory_mesh,trajectory_mesh_name);
    }

    if ( frustum_mesh->V.rows() > 0 ){
        frustum_mesh->m_vis.m_point_size = 10;
        frustum_mesh->m_vis.set_color_pervertcolor();
        frustum_mesh->m_vis.m_show_points=true;
        frustum_mesh->m_vis.m_show_mesh=false;
        frustum_mesh->m_vis.m_show_lines=true;
        Scene::show(frustum_mesh,frustum_mesh_name);
    }
}


void Gui::init_style() {
    //based on https://www.unknowncheats.me/forum/direct3d/189635-imgui-style-settings.html
    ImGuiStyle *style = &ImGui::GetStyle();

    style->WindowPadding = ImVec2(15, 15);
    style->WindowRounding = 0.0f;
    style->FramePadding = ImVec2(5, 5);
    style->FrameRounding = 4.0f;
    style->ItemSpacing = ImVec2(12, 8);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing = 25.0f;
    style->ScrollbarSize = 8.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 5.0f;
    style->GrabRounding = 3.0f;

    style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.05f, 0.07f, 0.85f);
    style->Colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.83f, 0.0f);
    style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    // style->Colors[ImGuiCol_ComboBg] = ImVec4(0.19f, 0.18f, 0.21f, 1.00f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 0.35f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    // style->Colors[ImGuiCol_Column] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    // style->Colors[ImGuiCol_ColumnHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    // style->Colors[ImGuiCol_ColumnActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    // style->Colors[ImGuiCol_CloseButton] = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
    // style->Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
    // style->Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
    style->Colors[ImGuiCol_PlotLines] = ImVec4(0.63f, 0.6f, 0.6f, 0.94f);
    style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.63f, 0.6f, 0.6f, 0.94f);
    style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);

    style->Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_TabActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
}

} //namespace easy_pbr
