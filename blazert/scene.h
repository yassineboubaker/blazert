#pragma once
#ifndef BLAZERT_BLAZERT_SCENE_H
#define BLAZERT_BLAZERT_SCENE_H

#include <blazert/bvh/accel.h>
#include <blazert/bvh/options.h>
#include <blazert/datatypes.h>

#include <blazert/primitives/sphere.h>
#include <blazert/primitives/trimesh.h>

#include <blazert/ray.h>

namespace blazert {

template<typename T>
class BlazertScene {

public:
  BVHBuildOptions<T> build_options;
  BVHTraceOptions<T> trace_options;

  mutable bool has_been_committed = false;
  mutable unsigned int geometries = 0;

  TriangleMesh<T> triangles;
  TriangleSAHPred<T> triangles_sah;
  BVH<T> triangles_bvh;
  mutable bool has_triangles = false;

  Sphere<T> spheres;
  SphereSAHPred<T> spheres_sah;
  BVH<T> spheres_bvh;
  mutable bool has_spheres = false;

public:
  BlazertScene() = default;

  /**
   * @brief Adds a triangular mesh to the scene
   * @param vertices Vertices need to be allocated on the heap!
   * @param triangles Triangles need to be allocated on the heap!
   * @return Returns the (geometry) id for the mesh.
   * The geom_id is set in the rayhit structure by the intersection functions.
   */
  unsigned int add_mesh(const Vec3rList<T> &vertices, const Vec3iList &triangles);
  unsigned int add_spheres(const Vec3rList<T> &centers, const std::vector<T> &radii);

  bool commit() {

    if (has_triangles) {
      triangles_bvh.build(triangles, triangles_sah, build_options);
    }

    if (has_spheres) {
      spheres_bvh.build(spheres, spheres_sah, build_options);
    }

    has_been_committed = true;
    return has_been_committed;
  };
};

// TODO: Performance critical code should not be a member function (hidden pointer *this), since the compiler will not know how to optimize.
template<typename T>
inline bool intersect1(const BlazertScene<T> &scene, const Ray<T> &ray, RayHit<T> &rayhit) {

  // This may not be optimal, but the interface is simple and everything can (and will) be in-lined.
  RayHit<T> temp_rayhit;
  bool hit = false;

  // Do the traversal for all primitives ...
  if (scene.has_triangles) {
    TriangleIntersector<T> triangle_intersector{*(scene.triangles.vertices), *(scene.triangles.faces)};
    const bool hit_mesh = traverse(scene.triangles_bvh, ray, triangle_intersector, temp_rayhit, scene.trace_options);
    if (hit_mesh) {
      rayhit = temp_rayhit;
      hit += hit_mesh;
    }
  }

  if (scene.has_spheres) {
    SphereIntersector<T> sphere_intersector{*(scene.spheres.centers), *(scene.spheres.radii)};
    const bool hit_sphere = traverse(scene.spheres_bvh, ray, sphere_intersector, temp_rayhit, scene.trace_options);
    if (hit_sphere) {
      if (temp_rayhit.hit_distance < rayhit.hit_distance) {
        rayhit = temp_rayhit;
        hit += hit_sphere;
      }
    }
  }

  return hit;
}

// Implementation of the add_ functions goes below ..
template<typename T>
unsigned int BlazertScene<T>::add_mesh(const Vec3rList<T> &vertices, const Vec3iList &faces) {

  if ((!has_triangles) && (!has_been_committed)) {
    triangles = TriangleMesh(vertices, faces);
    triangles_sah = TriangleSAHPred(vertices, faces);
    has_triangles = true;

    return geometries++;
  }
  else {
    return -1;
  }
}

template<typename T>
unsigned int BlazertScene<T>::add_spheres(const Vec3rList<T> &centers, const std::vector<T> &radii) {

  if ((!has_spheres) && (!has_been_committed)) {
    spheres = Sphere(centers, radii);
    spheres_sah = SphereSAHPred(centers, radii);
    has_spheres = true;

    return geometries++;
  }
  else {
    return -1;
  }
}

}// namespace blazert

#endif//BLAZERT_BLAZERT_SCENE_H
