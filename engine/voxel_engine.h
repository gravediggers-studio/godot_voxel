#ifndef VOXEL_ENGINE_H
#define VOXEL_ENGINE_H

#include "../meshers/voxel_mesher.h"
#include "../util/containers/slot_map.h"
#include "../util/containers/std_vector.h"
#include "../util/godot/classes/rendering_device.h"
#include "../util/io/file_locker.h"
#include "../util/memory/memory.h"
#include "../util/string/std_string.h"
#include "../util/tasks/progressive_task_runner.h"
#include "../util/tasks/threaded_task_runner.h"
#include "../util/tasks/time_spread_task_runner.h"
#include "ids.h"
#include "priority_dependency.h"

#ifdef VOXEL_ENABLE_SMOOTH_MESHING
#include "detail_rendering/detail_rendering.h"
#endif

#ifdef VOXEL_ENABLE_INSTANCER
#include "../streams/instance_data.h"
#endif

#ifdef VOXEL_ENABLE_GPU
#include "gpu/compute_shader.h"
#include "gpu/gpu_storage_buffer_pool.h"
#include "gpu/gpu_task_runner.h"
#endif

ZN_GODOT_FORWARD_DECLARE(class RenderingDevice);
#ifdef ZN_GODOT_EXTENSION
using namespace godot;
#endif

namespace zylann::voxel {

// Singleton for common things, notably the task system and shared viewers list.
// In Godot terminology this used to be called a "server", but I don't really agree with the term here, and it can be
// confused with networking features.
class VoxelEngine {
public:
	struct BlockMeshOutput {
		enum Type {
			TYPE_MESHED, // Contains mesh
			TYPE_DROPPED // Indicates the meshing was cancelled
		};

		Type type;
		VoxelMesher::Output surfaces;
		// Only used if `has_mesh_resource` is true (usually when meshes are allowed to be build in threads). Otherwise,
		// mesh data will be in `surfaces` and has to be built on the main thread.
		Ref<Mesh> mesh;
		Ref<Mesh> shadow_occluder_mesh;
		// Remaps Mesh surface indices to Mesher material indices. Only used if `has_mesh_resource` is true.
		// TODO Optimize: candidate for small vector optimization. A big majority of meshes will have a handful of
		// surfaces, which would fit here without allocating.
		StdVector<uint16_t> mesh_material_indices;
		// In mesh block coordinates
		Vector3i position;
		// TODO Rename lod_index
		uint8_t lod;
		// Tells if the mesh resource was built as part of the task. If not, you need to build it on the main thread if
		// it is needed.
		bool has_mesh_resource;
		// Tells if the meshing task was required to build a rendering mesh if possible.
		bool visual_was_required;
#ifdef VOXEL_ENABLE_SMOOTH_MESHING
		// Can be null. Attached to meshing output so it is tracked more easily, because it is baked asynchronously
		// starting from the mesh task, and it might complete earlier or later than the mesh.
		std::shared_ptr<DetailTextureOutput> detail_textures;
#endif
	};

	struct BlockDataOutput {
		enum Type { //
			TYPE_LOADED,
			TYPE_GENERATED,
			TYPE_SAVED
		};

		Type type;
		// If voxels are null with TYPE_LOADED, it means no block was found in the stream (if any) and no generator task
		// was scheduled. This is the case when we don't want to cache blocks of generated data.
		std::shared_ptr<VoxelBuffer> voxels;
#ifdef VOXEL_ENABLE_INSTANCER
		UniquePtr<InstanceBlockData> instances;
#endif
		Vector3i position;
		uint8_t lod_index;
		bool dropped;
		bool max_lod_hint;
		// Blocks with this flag set should not be ignored.
		// This is used when data streaming is off, all blocks are loaded at once.
		// TODO Unused?
		bool initial_load;
		bool had_instances;
		bool had_voxels;
	};

#ifdef VOXEL_ENABLE_SMOOTH_MESHING
	struct BlockDetailTextureOutput {
		std::shared_ptr<DetailTextureOutput> detail_textures;
		Vector3i position;
		uint32_t lod_index;
	};
#endif

	struct VolumeCallbacks {
		void (*mesh_output_callback)(void *, BlockMeshOutput &) = nullptr;
		void (*data_output_callback)(void *, BlockDataOutput &) = nullptr;
#ifdef VOXEL_ENABLE_SMOOTH_MESHING
		void (*detail_texture_output_callback)(void *, BlockDetailTextureOutput &) = nullptr;
#endif
		void *data = nullptr;

		inline bool check_callbacks() const {
			ZN_ASSERT_RETURN_V(mesh_output_callback != nullptr, false);
			ZN_ASSERT_RETURN_V(data_output_callback != nullptr, false);
			// ZN_ASSERT_RETURN_V(normalmap_output_callback != nullptr, false);
			ZN_ASSERT_RETURN_V(data != nullptr, false);
			return true;
		}
	};

	struct Viewer {
		struct Distances {
			unsigned int horizontal = 128;
			unsigned int vertical = 128;

			inline unsigned int max() const {
				return math::max(horizontal, vertical);
			}
		};
		// enum Flags {
		// 	FLAG_DATA = 1,
		// 	FLAG_VISUAL = 2,
		// 	FLAG_COLLISION = 4,
		// 	FLAGS_COUNT = 3
		// };
		Vector3 world_position;
		Distances view_distances;
		bool require_collisions = true;
		bool require_visuals = true;
		bool requires_data_block_notifications = false;
		int network_peer_id = -1;
	};

	static constexpr unsigned int DEFAULT_MAIN_THREAD_BUDGET_USEC = 8000;

	struct Config {
		int thread_count_minimum = 1;
		// How many threads below available count on the CPU should we set as limit
		int thread_count_margin_below_max = 1;
		// Portion of available CPU threads to attempt using
		float thread_count_ratio_over_max = 0.5;
		unsigned int main_thread_budget_usec = DEFAULT_MAIN_THREAD_BUDGET_USEC;
	};

	static VoxelEngine &get_singleton();
	static void create_singleton(Config config);
	static void destroy_singleton();

	// This is a separate initialization step due to GDExtension limitations.
	// It must be called when RenderingServer singleton is available (which is not the case with GDExtension during
	// class registrations, contrary to modules).
	// See https://github.com/godotengine/godot-cpp/issues/1180
	void try_initialize_gpu_features();

	VolumeID add_volume(VolumeCallbacks callbacks);
	VolumeCallbacks get_volume_callbacks(VolumeID volume_id) const;

	void remove_volume(VolumeID volume_id);
	bool is_volume_valid(VolumeID volume_id) const;

	std::shared_ptr<PriorityDependency::ViewersData> get_shared_viewers_data_from_default_world() const {
		return _world.shared_priority_dependency;
	}

	ViewerID add_viewer();
	void remove_viewer(ViewerID viewer_id);
	void set_viewer_position(ViewerID viewer_id, Vector3 position);
	void set_viewer_distances(ViewerID viewer_id, Viewer::Distances distances);
	Viewer::Distances get_viewer_distances(ViewerID viewer_id) const;
	void set_viewer_requires_visuals(ViewerID viewer_id, bool enabled);
	bool is_viewer_requiring_visuals(ViewerID viewer_id) const;
	void set_viewer_requires_collisions(ViewerID viewer_id, bool enabled);
	bool is_viewer_requiring_collisions(ViewerID viewer_id) const;
	void set_viewer_requires_data_block_notifications(ViewerID viewer_id, bool enabled);
	bool is_viewer_requiring_data_block_notifications(ViewerID viewer_id) const;
	void set_viewer_network_peer_id(ViewerID viewer_id, int peer_id);
	int get_viewer_network_peer_id(ViewerID viewer_id) const;
	bool viewer_exists(ViewerID viewer_id) const;
	void sync_viewers_task_priority_data();
	bool get_viewer_count() const;

	template <typename F>
	inline void for_each_viewer(F f) const {
		_world.viewers.for_each_key_value(f);
	}

	void push_main_thread_time_spread_task(
			ITimeSpreadTask *task,
			TimeSpreadTaskRunner::Priority priority = TimeSpreadTaskRunner::PRIORITY_NORMAL
	);
	int get_main_thread_time_budget_usec() const;
	void set_main_thread_time_budget_usec(unsigned int usec);

	// This should be fast and safe to access from multiple threads.
	bool is_threaded_graphics_resource_building_enabled() const;
	// void set_threaded_graphics_resource_building_enabled(bool enabled);

	void push_main_thread_progressive_task(IProgressiveTask *task);

	// Thread-safe.
	void push_async_task(IThreadedTask *task);
	// Thread-safe.
	void push_async_tasks(Span<IThreadedTask *> tasks);
	// Thread-safe.
	void push_async_io_task(IThreadedTask *task);
	// Thread-safe.
	void push_async_io_tasks(Span<IThreadedTask *> tasks);

#ifdef VOXEL_ENABLE_GPU
	void push_gpu_task(IGPUTask *task);

	template <typename F>
	void push_gpu_task_f(F f) {
		struct Task : public IGPUTask {
			F f;
			Task(F pf) : f(pf) {}
			void prepare(GPUTaskContext &ctx) override {
				f(ctx);
			}
			void collect(GPUTaskContext &ctx) override {}
		};
		push_gpu_task(ZN_NEW(Task(f)));
	}

	uint32_t get_pending_gpu_tasks_count() const;
#endif

	void process();
	void wait_and_clear_all_tasks(bool warn);

	inline FileLocker &get_file_locker() {
		return _file_locker;
	}

	static inline int get_octree_lod_block_region_extent(float lod_distance, float block_size) {
		// This is a bounding radius of blocks around a viewer within which we may load them.
		// `lod_distance` is the distance under which a block should subdivide into a smaller one.
		// Each LOD is fractal so that value is the same for each of them, multiplied by 2^lod.
		return static_cast<int>(Math::ceil(lod_distance / block_size)) * 2 + 2;
	}

	struct Stats {
		struct ThreadPoolStats {
			unsigned int thread_count;
			unsigned int active_threads;
			unsigned int tasks;
			FixedArray<const char *, ThreadedTaskRunner::MAX_THREADS> active_task_names;
		};

		ThreadPoolStats general;
		int generation_tasks;
		int streaming_tasks;
		int meshing_tasks;
		int main_thread_tasks;
#ifdef VOXEL_ENABLE_GPU
		int gpu_tasks;
#endif
	};

	Stats get_stats() const;

	int get_thread_count() const;
	void set_thread_count(uint32_t count);

#ifdef VOXEL_ENABLE_GPU
	bool has_rendering_device() const {
		return _gpu_task_runner.has_rendering_device();
	}
#endif

	// RenderingDevice &get_rendering_device() const {
	// 	ZN_ASSERT(_rendering_device != nullptr);
	// 	return *_rendering_device;
	// }

	// TODO Should be private, but can't because `memdelete<T>` would be unable to call it otherwise...
	~VoxelEngine();

	inline void debug_increment_generate_block_task_counter() {
		// Need to conditionally do this to avoid "unused variable" warnings in non-profiling builds
#ifdef ZN_PROFILER_ENABLED
		int64_t v =
#endif
				++_debug_generate_block_task_count;
#ifdef ZN_PROFILER_ENABLED
		ZN_PROFILE_PLOT("GenerateBlock* tasks", v);
#endif
	}

	inline void debug_decrement_generate_block_task_counter() {
#ifdef ZN_PROFILER_ENABLED
		int64_t v =
#endif
				--_debug_generate_block_task_count;
#ifdef ZN_PROFILER_ENABLED
		ZN_PROFILE_PLOT("GenerateBlock* tasks", v);
#endif
	}

private:
	VoxelEngine(Config config);

	// Since we are going to send data to tasks running in multiple threads, a few strategies are in place:
	//
	// - Copy the data for each task. This is suitable for simple information that doesn't change after scheduling.
	//
	// - Per-thread instances. This is done if some heap-allocated class instances are not safe
	//   to use in multiple threads, and don't change after being scheduled.
	//
	// - Shared pointers. This is used if the data can change after being scheduled.
	//   This is often done without locking, but only if it's ok to have dirty reads.
	//   If such data sets change structurally (like their size, or other non-dirty-readable fields),
	//   then a new instance is created and old references are left to "die out".

	struct Volume {
		VolumeCallbacks callbacks;
	};

	struct World {
		SlotMap<Volume, uint16_t, uint16_t> volumes;
		SlotMap<Viewer, uint16_t, uint16_t> viewers;

		// Must be overwritten with a new instance if count changes.
		std::shared_ptr<PriorityDependency::ViewersData> shared_priority_dependency;
	};

	// TODO multi-world support in the future
	World _world;

	ThreadedTaskRunner _general_thread_pool;
	// For tasks that can only run on the main thread and be spread out over frames
	TimeSpreadTaskRunner _time_spread_task_runner;
	unsigned int _main_thread_time_budget_usec = DEFAULT_MAIN_THREAD_BUDGET_USEC;
	ProgressiveTaskRunner _progressive_task_runner;

	FileLocker _file_locker;

	// Caches whether building Mesh and Texture resources is allowed from inside threads.
	// Depends on Godot's efficiency at doing so, and which renderer is used.
	// For example, the OpenGL renderer does not support this well, but the Vulkan one should.
	bool _threaded_graphics_resource_building_enabled = false;

#ifdef VOXEL_ENABLE_GPU
	GPUTaskRunner _gpu_task_runner;
#endif

	// There can be multiple types of generation tasks, so we count them with a common counter.
	std::atomic_int _debug_generate_block_task_count = { 0 };
};

struct VoxelFileLockerRead {
	VoxelFileLockerRead(const StdString &path) : _path(path) {
		VoxelEngine::get_singleton().get_file_locker().lock_read(path);
	}

	~VoxelFileLockerRead() {
		VoxelEngine::get_singleton().get_file_locker().unlock(_path);
	}

	StdString _path;
};

struct VoxelFileLockerWrite {
	VoxelFileLockerWrite(const StdString &path) : _path(path) {
		VoxelEngine::get_singleton().get_file_locker().lock_write(path);
	}

	~VoxelFileLockerWrite() {
		VoxelEngine::get_singleton().get_file_locker().unlock(_path);
	}

	StdString _path;
};

} // namespace zylann::voxel

#endif // VOXEL_ENGINE_H
