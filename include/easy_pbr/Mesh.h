#pragma once

#include <memory>
#include<stdarg.h>
#include <any>
#include <stdexcept>



//eigen
#include <Eigen/Geometry>

//opencv for textre
#include "opencv2/opencv.hpp"

//better enums
#include <enum.h>

// #include <loguru.hpp>


namespace radu { namespace utils {
    class RandGenerator;
    }}

namespace easy_pbr{

BETTER_ENUM(MeshColorType, int, Solid = 0, PerVertColor, Texture, SemanticPred, SemanticGT, NormalVector, Height, Intensity, UV, NormalViewCoords )
BETTER_ENUM(ColorSchemeType, int, Plasma = 0, Viridis, Magma )


class MeshGL; //we forward declare this so we can have from here a pointer to the gpu stuff
class LabelMngr;
class Mesh;
class Viewer;


struct VisOptions{
     //visualization params (it's nice to have here so that the various algorithms that run in different threads can set them)
    bool m_is_visible=true;
    bool m_force_cast_shadow=false;
    bool m_show_points=false;
    bool m_show_lines=false;
    bool m_show_normals=false;
    bool m_show_mesh=true;
    bool m_show_wireframe=false;
    bool m_show_surfels=false;
    bool m_show_vert_ids=false;
    bool m_show_vert_coords=false;
    bool m_use_custom_shader=false;

    //sometimes when you render lines or points you just want them on top of everything else so you just disable all depth testing for that mesh
    bool m_overlay_points=false;
    bool m_overlay_lines=false;

    bool m_points_as_circle=false;


    float m_point_size=4.0;
    float m_line_width=1.0; //specified the width of of both line rendering and the wireframe rendering
    float m_normals_scale=-1.0; //the scale of the arrows for the normal. It starts at -1.0 but it gets set during the first render to something depending on the mesh scale
    MeshColorType m_color_type=MeshColorType::Solid;
    ColorSchemeType m_color_scheme = ColorSchemeType::Plasma;
    // Eigen::Vector3f m_point_color = Eigen::Vector3f(1.0, 215.0/255.0, 85.0/255.0);
    Eigen::Vector3f m_point_color = Eigen::Vector3f(245.0/255.0, 175.0/255.0, 110.0/255.0);
    Eigen::Vector3f m_line_color = Eigen::Vector3f(1.0, 0.0, 0.0);   //used for lines and wireframes
    Eigen::Vector3f m_solid_color = Eigen::Vector3f(1.0, 206.0/255.0, 143.0/255.0);
    Eigen::Vector3f m_label_color = Eigen::Vector3f(1.0, 160.0/255.0, 0.0);
    float m_metalness=0.0;
    float m_roughness=0.35;

    //we define some functions for settings colors both for convenicence and easily calling then from python with pybind
    void set_color_solid(){
        m_color_type=MeshColorType::Solid;
    }
    void set_color_pervertcolor(){
        m_color_type=MeshColorType::PerVertColor;
    }
    void set_color_texture(){
        m_color_type=MeshColorType::Texture;
    }
    void set_color_semanticpred(){
        m_color_type=MeshColorType::SemanticPred;
    }
    void set_color_semanticgt(){
        m_color_type=MeshColorType::SemanticGT;
    }
    void set_color_normalvector(){
        m_color_type=MeshColorType::NormalVector;
    }
    void set_color_height(){
        m_color_type=MeshColorType::Height;
    }
    void set_color_intensity(){
        m_color_type=MeshColorType::Intensity;
    }
    void set_color_uv(){
        m_color_type=MeshColorType::UV;
    }
    void set_color_normalvector_viewcoords(){
        m_color_type=MeshColorType::NormalViewCoords;
    }

    bool operator==(const VisOptions& rhs) const{
        return
        this->m_is_visible == rhs.m_is_visible &&
        this->m_show_points == rhs.m_show_points &&
        this->m_show_lines == rhs.m_show_lines &&
        this->m_show_normals == rhs.m_show_normals &&
        this->m_show_mesh == rhs.m_show_mesh &&
        this->m_show_wireframe == rhs.m_show_wireframe &&
        this->m_show_surfels == rhs.m_show_surfels &&
        this->m_show_vert_ids == rhs.m_show_vert_ids &&
        this->m_show_vert_coords == rhs.m_show_vert_coords &&
        this->m_use_custom_shader == rhs.m_use_custom_shader &&
        this->m_overlay_points == rhs.m_overlay_points &&
        this->m_overlay_lines == rhs.m_overlay_lines &&
        this->m_points_as_circle == rhs.m_points_as_circle &&
        this->m_point_size == rhs.m_point_size &&
        this->m_line_width == rhs.m_line_width &&
        this->m_normals_scale == rhs.m_normals_scale &&
        this->m_color_type == rhs.m_color_type &&
        this->m_point_color == rhs.m_point_color &&
        this->m_line_color == rhs.m_line_color &&
        this->m_solid_color == rhs.m_solid_color &&
        this->m_label_color == rhs.m_label_color &&
        this->m_metalness == rhs.m_metalness &&
        this->m_roughness == rhs.m_roughness
        ;
    }
    bool operator!=(const VisOptions& rhs) const{
        return !(*this==rhs);
    }


};

//when uploading texture from cpu we want a way to say that this is dirty
struct CvMatCpu {
    cv::Mat mat;
    bool is_dirty=false;
};

//for matrices bookkeeping
template <typename T>
struct DataBlob {
    public:
        DataBlob(Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& data):
            m_data(data){
        }

        Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& data(){return m_data;} //get a reference to the internal matrix

        void preallocate(size_t rows, size_t cols){
            m_data.resize(rows,cols);
            m_data.setZero();
            m_is_preallocated=true;
        }
        void copy_in_first_empty_block(const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& new_data){
            //WARNING , cannot be used to append E and F because they also need to be reindexed
            static_assert(std::is_same<T, double>::value, "We only allow appending to matrices that are of type double. If the matrix of type int it means it is E or F and therefore would need to be reindexed after appending which is no supported in the streaming API");
            //other checks
            // CHECK(m_is_preallocated) << "Can only use this appening API when the data has been preallocated";
            // CHECK(m_data.cols()==new_data.cols()) << "Data cols and new_data cols do not coincide. m_data cols is " << m_data.cols() << " and new_data cols is " << new_data.cols();
            if (!m_is_preallocated){
                throw std::runtime_error( "Can only use this appening API when the data has been preallocated" );
            }
            if (m_data.cols()!=new_data.cols()){
                throw std::runtime_error( "Data cols and new_data cols do not coincide. m_data cols is " + std::to_string(m_data.cols()) + " and new_data cols is " + std::to_string(new_data.cols() )  );
            }
            


            //find a block that can fit the new data size
            //check at the finale of the unallocated data or the beggining
            int nr_empty_rows_finale= m_data.rows()-m_end_row_allocated;
            int nr_empty_rows_start= m_start_row_allocated;
            int start_new_block=-1;
            //try first to insert at the finale, if not, at the beggining and if not then we deem that we don't have enough space
            if(nr_empty_rows_finale>=new_data.rows()){
                start_new_block= m_end_row_allocated;
            }else if(nr_empty_rows_start>=new_data.rows()){
                start_new_block=0;
            }else{
                // LOG(WARNING) << "Dropping, cannot append anywhere inside this data matrix. We are trying to append a new matrix of rows " << new_data.rows() << ". The preallocated data has rows " << m_data.rows() << " the m_start_row_allocated is " << m_start_row_allocated << " m_end_row_allocated " <<m_end_row_allocated;
                std::cout <<  "Dropping, cannot append anywhere inside this data matrix. We are trying to append a new matrix of rows " << new_data.rows() << ". The preallocated data has rows " << m_data.rows() << " the m_start_row_allocated is " << m_start_row_allocated << " m_end_row_allocated " <<m_end_row_allocated << std::endl;
            }

            //copy in the empty block
            // m_data.block< new_data.rows(), new_data.cols() >(start_new_block,0)=new_data;
            m_data.block(start_new_block,0, new_data.rows(), new_data.cols() )=new_data;
            m_end_row_allocated+= new_data.rows();

        }
        void recycle_block(size_t start_row, size_t end_row){

        }

        bool is_preallocated(){ return m_is_preallocated; }


    private:
        Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& m_data;
        bool m_is_preallocated=false;
        size_t m_start_row_allocated=0;
        size_t m_end_row_allocated=0;
};

class Mesh : public std::enable_shared_from_this<Mesh>{ //enable_shared_from is required due to pybind https://pybind11.readthedocs.io/en/stable/advanced/smart_ptrs.html
public:
    // EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    template <class ...Args>
    static std::shared_ptr<Mesh> create( Args&& ...args ){
        return std::shared_ptr<Mesh>( new Mesh(std::forward<Args>(args)...) );
    }
    Mesh();
    Mesh(const std::string file_path);
    ~Mesh()=default;

    Mesh clone();
    void add(Mesh& new_mesh); //Adds another mesh to this one and combines it into one
    void add(const std::vector<std::shared_ptr<Mesh>>& meshes); //Adds all the meshes on the list onto this one
    void clear(); //empties all vectors makes them have size (0,0)
    void set_all_matrices_to_zero();
    void assign_mesh_gpu(std::shared_ptr<MeshGL> mesh_gpu); //assigns the pointer to the gpu implementation of this mesh
    bool load_from_file(const std::string file_path); //return sucess or failure
    void read_obj(const std::string file_path, bool load_vti=false, bool load_vni=false); //vti and vni which are the indices that the vertices have towards the textures and towards the normals. Check https://en.wikipedia.org/wiki/Wavefront_.obj_file about Vertex texture coordinate indices and Vertex normal indices
    void save_to_file(const std::string file_path);
    bool is_empty()const;
    // void apply_transform(Eigen::Affine3d& trans, const bool transform_points_at_zero=false ); //transforms the vertices V and the normals. A more efficient way would be to just update the model matrix and let the GPU do it but I like having the V here and on the GPU in sync so I rather transform on CPU and then send all the data to GPU
    // void transform_model_matrix(const Eigen::Affine3d& trans); //updates the model matrix but does not change the vertex data V on the CPU
    // void apply_transform(const Eigen::Affine3d& tf, const bool update_cpu_data, const bool transform_points_at_zero=false); //if we update the CPU data we move directly the V vertices but the model matrix does not get updates and it will stay as whatever it was set before. That means that the origin around which rotations will be applied subsequently may not lie anymore where you expected. If we have update_cpu_data to false, we only modify the model matrix therefore the model matrix

    Eigen::Affine3d cur_pose();
    Eigen::Affine3d& cur_pose_ref();
    void set_cur_pose(const Eigen::Affine3d& new_model_matrix);
    Eigen::Affine3d model_matrix();
    Eigen::Affine3d& model_matrix_ref();
    void set_model_matrix(const Eigen::Affine3d& new_model_matrix);
    void set_model_matrix_from_string(const std::string& pose_string); //pose_string contains x y z qx qy qz qw  with a space delimiter in between
    void transform_vertices_cpu(const Eigen::Affine3d& trans, const bool transform_points_at_zero=false); //modifyed the vertices on the cpu but does not update the model matrix
    void transform_model_matrix(const Eigen::Affine3d& trans); //just affects how the model is displayed when rendered by modifying the model matrix but does not change the vertices themselves
    void translate_model_matrix(const Eigen::Vector3d& translation); //easier acces to transform of model matrix by just translation. Easier to call from python
    void rotate_model_matrix(const Eigen::Vector3d& axis, const float angle_degrees);
    void rotate_model_matrix_local(const Eigen::Vector3d& axis, const float angle_degrees);
    void rotate_model_matrix_local(const Eigen::Quaterniond& q);
    void apply_model_matrix_to_cpu( const bool transform_points_at_zero);
    void scale_mesh(const float scale);
    // void set_model_matrix(const Eigen::VectorXd& xyz_q);
    // Eigen::VectorXd model_matrix_as_xyz_and_quaternion();
    // Eigen::VectorXd model_matrix_as_xyz_and_rpy();
    // void premultiply_model_matrix(const Eigen::VectorXd& xyz_q);
    // void postmultiply_model_matrix(const Eigen::VectorXd& xyz_q);

    //preallocation things
    void preallocate_V(size_t max_nr_verts); //we preallocate a certain nr of vertices,
    void preallocate_F(size_t max_nr_faces);
    void preallocate_C(size_t max_nr_verts);
    void preallocate_E(size_t max_nr_lines);
    void preallocate_D(size_t max_nr_verts);
    void preallocate_NF(size_t max_nr_faces);
    void preallocate_NV(size_t max_nr_verts);
    void preallocate_UV(size_t max_nr_verts);
    void preallocate_V_tangent_u(size_t max_nr_verts);
    void preallocate_V_length_v(size_t max_nr_verts);
    void preallocate_L_pred(size_t max_nr_verts);
    void preallocate_L_gt(size_t max_nr_verts);
    void preallocate_I(size_t max_nr_verts);




    void clear_C();
    void color_from_label_indices(Eigen::MatrixXi label_indices);
    void color_from_mat(const cv::Mat& mat); //sample the mat using the UVS and store the pixel values into C
    Eigen::Vector3d centroid();
    void sanity_check() const; //check that all the data inside the mesh is valid, there are enough normals for each face, faces don't idx invalid points etc.
    //create certain meshes
    void create_full_screen_quad();
    void create_box_ndc(); //makes a 1x1x1 vox in NDC. which has z going into the screen
    void create_box(const float w, const float h, const float l); //makes a box of a certain width, height and length which correspond to the x,y,z axes with y pointing up and x to the right
    void create_grid(const int nr_segments, const float y_pos, const float scale);
    void create_floor(const float y_pos, const float scale);
    void create_sphere(const Eigen::Vector3d& center, const double radius);
    void create_cylinder(const Eigen::Vector3d& main_axis, const double height, const double radius, const bool origin_at_bottom, const bool with_cap);
    void create_line_strip_from_points(const std::vector<Eigen::Vector3d>& points_vec);
    void add_child(std::shared_ptr<Mesh>& mesh); //add a child into the transformation hierarchy. Therefore when this object moves or rotates the children also do.

    //lots of mesh ops
    void remove_marked_vertices(const std::vector<bool>& mask, const bool keep);
    void set_marked_vertices_to_zero(const std::vector<bool>& mask, const bool keep); //useful for when the actual removal of verts will destroy the organized structure
    void remove_vertices_at_zero(); // zero is used to denote the invalid vertex, we can remove them and rebuild F, E and the rest of indices with this function
    void remove_unreferenced_verts();
    Eigen::VectorXi remove_duplicate_vertices(); //return the inverse_indirection which is a vector of size original_mesh.V.rows(). Says for each original V where it's now indexed in the merged mesh
    void undo_remove_duplicate_vertices(const std::shared_ptr<Mesh>& original_mesh, const Eigen::VectorXi& inverse_indirection );
    void set_duplicate_verts_to_zero();
    void decimate(const int nr_target_faces);
    void upsample(const int nr_of_subdivisions, const bool smooth);
    void flip_winding(); //flips the winding number for the faces
    bool compute_non_manifold_edges(std::vector<bool>& is_face_non_manifold, std::vector<bool>& is_vertex_non_manifold,  const Eigen::MatrixXi& F_in);
    void rotate_90_x_axis();
    void worldGL2worldROS();
    void worldROS2worldGL();
    // void rotate_x_axis(const float degrees);
    // void rotate_y_axis(const float degrees);
    void random_subsample(const float percentage_removal);
    void recalculate_normals(); //recalculates NF and NV
    void flip_normals();
    void normalize_size(); //normalize the size of the mesh between [0,1]
    void normalize_position(); //calculate the bounding box of the object and put it at 0.0.0
    void recalculate_min_max_height();
    Eigen::VectorXi fix_oversplit_due_to_blender_uv();
    void color_connected_components();
    void remove_small_uv_charts();
    void apply_D(); //given the D which is the distance of the vertices from the origin of the senser, applies it to the vertices
    void to_image();
    void to_image(Eigen::Affine3d tf_currframe_alg);
    void to_mesh();
    void to_mesh(Eigen::Affine3d tf_currframe_alg);
    void to_3D();   //from a matrix with 2 columns creates one with 3 columns (requiered to go from the delaunay triangulation into a 3d mesh representable in libigl)
    void to_2D(); //from a matrix V with 3 columns, it discards te last one and creates one with 2 columns (in order for an image to be passed to the triangle library)
    void restrict_around_azimuthal_angle(const float angle, const float range); //for an organized point cloud, sets to zero the points that are not around a certain azimuthal angle when viewed from the algorithm frame
    void compute_tangents(const float tagent_length=1.0);
    void as_uv_mesh_paralel_to_axis(const int axis, const float size_modifier);
    Mesh interpolate(const Mesh& target_mesh, const float factor);
    float get_scale();
    void color_solid2pervert(); //makes the solid color into a per vert color by allocating a C vector. It is isefult when merging meshes of different colors.
    void estimate_normals_from_neighbourhood(const float radius);
    Eigen::MatrixXd compute_distance_to_mesh(const std::shared_ptr<Mesh>& target_mesh); //compute for each point in this cloud, the distance to another one, returns a D vector of distances which is of size Nx1 where N is the vertices in this mesh


    //nanoflann options for querying points in a certain radius or querying neighbiurs
    int radius_search(const Eigen::Vector3d& query_point, const double radius); //returns touples of (index in V of the point, distance to it)
    void ball_query(const Eigen::MatrixXd& query_points, const Eigen::MatrixXd& target_points, const float search_radius);

    //some convenience functions and also useful for calling from python using pybind
    // void move_in_x(const float amount);
    // void move_in_y(const float amount);
    // void move_in_z(const float amount);
    void random_translation(const float translation_strength);
    void random_rotation(const float rotation_strength); //applies a rotation to all axes, of maximum rotation_strength degrees
    void random_stretch(const float stretch_strength); //stretches the x,y,z axis by a random number
    void random_noise(const float noise_stddev); //adds random gaussian noise with some strength to each points

    //return the min y and min max flor plotting. May not neceserally be the min max of the actual data. The min_max_y_plotting is controlable in the gui
    float min_y();
    float max_y();

    //set textures for pbr
    void set_diffuse_tex(const std::string file_path, const int subsample=1, const bool read_alpha=false);
    void set_metalness_tex(const std::string file_path, const int subsample=1, const bool read_alpha=false);
    void set_roughness_tex(const std::string file_path, const int subsample=1, const bool read_alpha=false);
    void set_smoothness_tex(const std::string file_path, const int subsample=1, const bool read_alpha=false); //the inverse of the roughness
    void set_normals_tex(const std::string file_path, const int subsample=1, const bool read_alpha=false);
    //using a mat directly
    void set_diffuse_tex(const cv::Mat& mat, const int subsample=1);
    void set_metalness_tex(const cv::Mat& mat, const int subsample=1);
    void set_roughness_tex(const cv::Mat& mat, const int subsample=1);
    void set_smoothness_tex(const cv::Mat& mat, const int subsample=1);
    void set_normals_tex(const cv::Mat& mat, const int subsample=1);
    bool is_any_texture_dirty();


    friend std::ostream &operator<<(std::ostream&, const Mesh& m);

    bool m_is_dirty; // if it's dirty then we need to upload this data to the GPU
    bool m_is_shadowmap_dirty; // if it has moved through the m_model_matrix or if the V matrix or something like that has changed, then we need to update the shadow map

    VisOptions m_vis;
    bool m_force_vis_update; //sometimes we want the m_vis stored in the this MeshCore to go into the MeshGL, sometimes we don't. The default is to not propagate, setting this flag to true will force the update of m_vis inside the MeshGL


    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    Eigen::MatrixXd C;
    Eigen::MatrixXi E;
    Eigen::MatrixXd D;  //distances of points to the sensor
    Eigen::MatrixXd NF; //normals of each face
    Eigen::MatrixXd NV; //normals of each vertex
    Eigen::MatrixXd UV; //UV for each vertex
    Eigen::MatrixXd V_tangent_u; //for surfel rendering each vertex has a 2 vectors that are tangent defining the span of the elipsoid. For memory usage we don't store the 2 vectors directly because we alreayd have a normal vector, rather we store one tangent vector in full (vec3) and the other one we store only the norm of it because it's dirrection can be inferred as the cross product between the normal and the first tangent vector
    Eigen::MatrixXd V_length_v;
    Eigen::MatrixXd V_bitangent_v;
    Eigen::MatrixXd S_pred; //predicted likelihood for each class per point, useful for semantic segmentation
    Eigen::MatrixXi L_pred; //predicted labels for each point, useful for semantic segmentation
    Eigen::MatrixXi L_gt; //ground truth labels for each point, useful for semantic segmentation
    Eigen::MatrixXd I; //intensity value of each point in the cloud. Useful for laser scanner
    Eigen::MatrixXi VTI; //Vertex texture coordinate indices which is the coordinate that the vertices has towards the UV. check https://en.wikipedia.org/wiki/Wavefront_.obj_file
    Eigen::MatrixXi VNI; //Vertex normal indices which is the coordinate that the vertices has towards the NV. check https://en.wikipedia.org/wiki/Wavefront_.obj_file
    DataBlob<double> V_blob;


    int m_seg_label_pred; // for classification we will have a lable for the whole cloud
    int m_seg_label_gt;

    // cv::Mat m_rgb_tex_cpu;
    CvMatCpu m_diffuse_mat;
    CvMatCpu m_metalness_mat;
    CvMatCpu m_roughness_mat;
    CvMatCpu m_normals_mat;

    std::weak_ptr<MeshGL> m_mesh_gpu; // a pointer to the gpu implementation of this mesh, needs ot be weak because the mesh already has a shared ptr to the MeshCore
    std::shared_ptr<LabelMngr> m_label_mngr;
    std::shared_ptr<radu::utils::RandGenerator> m_rand_gen;
    std::vector<std::shared_ptr<Mesh>> m_child_meshes;
    std::function<void( std::shared_ptr<MeshGL> mesh_gl, std::shared_ptr<Viewer> view )> custom_render_func; //use this render the mesh with whatever function we define

    //oher stuff that may or may not be needed depending on the application
    uint64_t t; //timestamp or scan nr which will be monotonically increasing
    int id; //id number that can be used to identity meshes of the same type (vegation, people etc)
    int m_height;
    int m_width;
    float m_view_direction; //direction in which the points have been removed from the velodyne cloud so that it can be unwrapped easier into 2D
    Eigen::Vector2f m_min_max_y; //the min and the max coordinate in y direction. useful for plotting color based on height
    Eigen::Vector2f m_min_max_y_for_plotting; //sometimes we want the min and max to be a bit different (controlable through the gui)
    std::string m_disk_path; //path that from disk that was used to load this mesh


    //identification
    std::string name;



    //adding extra field to this can eb done through https://stackoverflow.com/a/50956105
    std::map<std::string, std::any> extra_fields;
    template <typename T>
    void add_extra_field(const std::string name, const T data){
        extra_fields[name] = data;
    }
    bool has_extra_field(const std::string name){
        if ( extra_fields.find(name) == extra_fields.end() ) {
            return false;
        } else {
            return true;
        }
    }
    template <typename T>
    T get_extra_field(const std::string name){
        // CHECK(has_extra_field(name)) << "The field you want to acces with name " << name << " does not exist. Please add it with add_extra_field";
        if (!has_extra_field(name)){
            throw std::runtime_error( "The field you want to acces with name " + name + " does not exist. Please add it with add_extra_field" );
        }
        return std::any_cast<T>(extra_fields[name]);
    }



private:

    std::string named(const std::string msg) const{
        return name.empty()? msg : name + ": " + msg;
    }


    //We use this for reading ply files because the readPLY from libigl has a memory leak https://github.com/libigl/libigl/issues/919
    void read_ply(const std::string file_path);
    void write_ply(const std::string file_path);

    Eigen::Affine3d m_model_matrix;  //transform from object coordiantes to the world coordinates, esentially putting the model somewhere in the world.
    Eigen::Affine3d m_cur_pose;

    //preallocation stuff
    bool m_is_preallocated;


};

typedef std::shared_ptr<Mesh> MeshSharedPtr;


} //namespace easy_pbr
