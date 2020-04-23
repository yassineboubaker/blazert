#pragma once
#ifndef BLAZERT_H_
#define BLAZERT_H_

#include <memory>
#include <vector>

#include <blazert/bvh/options.h>
#include <blazert/datatypes.h>
#include <blazert/ray.h>

/**
 * BLAZERT_USE_CPP11_FEATURE : Enable C++11 feature
 * BLAZERT_ENABLE_PARALLEL_BUILD : Enable parallel BVH build.
 * BLAZERT_ENABLE_SERIALIZATION : Enable serialization feature for built BVH.
 *
 * Parallelized BVH build is supported on C++11 thread version.
 * OpenMP version is not fully tested.
 * Thus turn off if you face a problem when building BVH in parallel.
 */

namespace blazert {

template<typename T>
class TriangleMesh {

public:
  const Vec3rList<T> *vertices_;
  const Vec3iList *faces_;

public:
  TriangleMesh(const Vec3rList<T> &vertices, const Vec3iList &faces) : vertices_(&vertices), faces_(&faces) {}

  /**
   * Compute bounding box for `prim_index`th triangle.
   * This function is called for each primitive in BVH build
   */
  inline unsigned int size() const {
    return (*faces_).size();
  }
  inline void BoundingBox(Vec3r<T> &bmin, Vec3r<T> &bmax, unsigned int prim_index) const {

    const Vec3i face = (*faces_)[prim_index];

    {
      const Vec3r<T> vertex = (*vertices_)[face[0]];
      bmin = bmax = vertex;
    }

    for (unsigned int i = 1; i < 3; i++) {
      const Vec3r<T> vertex = (*vertices_)[face[i]];
      for (int k = 0; k < 3; k++) {
        bmin[k] = blaze::min(bmin[k], vertex[k]);
        bmax[k] = blaze::max(bmax[k], vertex[k]);
      }
    }
  }
};

// Predefined SAH predicator for triangle.
template<typename T>
class TriangleSAHPred {
private:
  mutable unsigned int axis_;
  mutable T pos_;
  const Vec3rList<T> *vertices_;
  const Vec3iList *faces_;

public:
  TriangleSAHPred(const Vec3rList<T> &vertices, const Vec3iList &faces)
      : axis_(0), pos_(static_cast<T>(0.0)), vertices_(&vertices), faces_(&faces) {}

  TriangleSAHPred(const TriangleSAHPred<T> &rhs)
      : axis_(rhs.axis_),
        pos_(rhs.pos_),
        vertices_(rhs.vertices_),
        faces_(rhs.faces_) {}

  TriangleSAHPred<T> &operator=(const TriangleSAHPred<T> &rhs) {
    axis_ = rhs.axis_;
    pos_ = rhs.pos_;
    vertices_ = rhs.vertices_;
    faces_ = rhs.faces_;
    return (*this);
  }

  void Set(unsigned int axis, T pos) const {
    axis_ = axis;
    pos_ = pos;
  }

  /// The operator returns true, if the triable center is within a margin of 3 time the position along a specified axis
  bool operator()(unsigned int prim_id) const {

    const Vec3i face = (*faces_)[prim_id];
    const Vec3r<T> p0 = (*vertices_)[face[0]];
    const Vec3r<T> p1 = (*vertices_)[face[0]];
    const Vec3r<T> p2 = (*vertices_)[face[0]];

    const T center = p0[axis_] + p1[axis_] + p2[axis_];

    // TODO: What the fuck ...? Where is the ```3.0``` coming from?
    return (center < pos_ * static_cast<T>(3.0));
  }
};

template<typename T>
class TriangleIntersection {
public:
  Vec2r<T> uv;
  T t;
  unsigned int prim_id = -1;
};

template<typename T>
struct alignas(sizeof(Vec3r<T>)) RayCoeff {
  Vec3r<T> S;
  Vec3i k;
};

template<typename T, class H = TriangleIntersection<T>>
class TriangleIntersector {
private:
  const Vec3rList<T> *vertices_;
  const Vec3iList *faces_;

  mutable Vec3r<T> ray_org_;
  mutable RayCoeff<T> ray_coeff_;
  mutable BVHTraceOptions trace_options_;
  mutable T t_min_;

  mutable T t_;
  mutable Vec2r<T> uv_;
  mutable unsigned int prim_id_;

public:
  inline TriangleIntersector(const Vec3rList<T> &vertices, const Vec3iList &faces) : vertices_(&vertices), faces_(&faces) {}

  /// Return the closest hit distance
  inline T GetT() const { return t_; }

  /// Update is called when initializing intersection and nearest hit is found.
  inline void Update(T t, unsigned int prim_id) const {
    t_ = t;
    prim_id_ = prim_id;
  }

  /**
   * Post BVH traversal stuff.
   * Fill `isect` if there is a hit.
   * TODO: Is Ray<T> really needed here?
   */
  inline void PostTraversal(const Ray<T> &ray, bool hit, H &intersection) const {
    if (hit) {
      intersection.t = t_;
      intersection.uv = uv_;
      intersection.prim_id = prim_id_;
    }
  }

  /**
   * Prepare BVH traversal (e.g. compute inverse ray direction).
   * This function is called only once in BVH traversal.
   */
  inline void PrepareTraversal(const Ray<T> &ray, const BVHTraceOptions &trace_options) const {

    ray_org_ = ray.org;

    // Calculate dimension where the ray direction is maximal.
    const Vec3r<T> ray_dir_abs = abs(ray.dir);
    ray_coeff_.k[2] = argmax(ray_dir_abs);// 0,1,2 -> 2,3,4 % 3 = 2,0,1

    if (ray.dir[ray_coeff_.k[2]] >= 0.) {
      ray_coeff_.k[0] = ray_coeff_.k[2] + 1;// 1,0,2 ->
      ray_coeff_.k[1] = ray_coeff_.k[0] + 1;//
    } else {
      ray_coeff_.k[1] = ray_coeff_.k[2] + 1;// 1,0,2 ->
      ray_coeff_.k[0] = ray_coeff_.k[1] + 1;//
    }

    for (int i=0; i<3; i++) {
      ray_coeff_.k[i] %= 3;
    }

    ray_coeff_.S[0] = ray.dir[ray_coeff_.k[0]] / ray.dir[ray_coeff_.k[2]];
    ray_coeff_.S[1] = ray.dir[ray_coeff_.k[1]] / ray.dir[ray_coeff_.k[2]];
    ray_coeff_.S[2] = static_cast<T>(1.0) / ray.dir[ray_coeff_.k[2]];

    trace_options_ = trace_options;

    t_min_ = ray.min_t;
    uv_ = 0.;
  }

  /**
   * Do ray intersection stuff for `prim_index` th primitive and return hit
   * distance `t`, barycentric coordinate `u` and `v`.
   * Returns true if there's intersection.
   */
  inline bool Intersect(T *t_inout, const unsigned int prim_index) const {
    if ((prim_index < trace_options_.prim_ids_range[0]) || (prim_index >= trace_options_.prim_ids_range[1])) {
      return false;
    }

    // Self-intersection test.
    if (prim_index == trace_options_.skip_prim_id) {
      return false;
    }

    const Vec3i face = (*faces_)[prim_index];
    const Vec3r<T> p0 = (*vertices_)[face[0]];
    const Vec3r<T> p1 = (*vertices_)[face[1]];
    const Vec3r<T> p2 = (*vertices_)[face[2]];
    const Vec3r<T> A = p0 - ray_org_;
    const Vec3r<T> B = p1 - ray_org_;
    const Vec3r<T> C = p2 - ray_org_;

    const T Ax = A[ray_coeff_.k[0]] - ray_coeff_.S[0] * A[ray_coeff_.k[2]];
    const T Ay = A[ray_coeff_.k[1]] - ray_coeff_.S[1] * A[ray_coeff_.k[2]];
    const T Bx = B[ray_coeff_.k[0]] - ray_coeff_.S[0] * B[ray_coeff_.k[2]];
    const T By = B[ray_coeff_.k[1]] - ray_coeff_.S[1] * B[ray_coeff_.k[2]];
    const T Cx = C[ray_coeff_.k[0]] - ray_coeff_.S[0] * C[ray_coeff_.k[2]];
    const T Cy = C[ray_coeff_.k[1]] - ray_coeff_.S[1] * C[ray_coeff_.k[2]];

    T U = Cx * By - Cy * Bx;
    T V = Ax * Cy - Ay * Cx;
    T W = Bx * Ay - By * Ax;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif

    // Fall back to test against edges using double precision.
    if (U == static_cast<T>(0.0) || V == static_cast<T>(0.0) || W == static_cast<T>(0.0)) {
      double CxBy = static_cast<double>(Cx) * static_cast<double>(By);
      double CyBx = static_cast<double>(Cy) * static_cast<double>(Bx);
      U = static_cast<T>(CxBy - CyBx);

      double AxCy = static_cast<double>(Ax) * static_cast<double>(Cy);
      double AyCx = static_cast<double>(Ay) * static_cast<double>(Cx);
      V = static_cast<T>(AxCy - AyCx);

      double BxAy = static_cast<double>(Bx) * static_cast<double>(Ay);
      double ByAx = static_cast<double>(By) * static_cast<double>(Ax);
      W = static_cast<T>(BxAy - ByAx);
    }

    if (U < static_cast<T>(0.0) || V < static_cast<T>(0.0) || W < static_cast<T>(0.0)) {
      if (trace_options_.cull_back_face || (U > static_cast<T>(0.0) || V > static_cast<T>(0.0) || W > static_cast<T>(0.0))) {
        return false;
      }
    }

    T det = U + V + W;
    if (det == static_cast<T>(0.0))
      return false;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

    const T Az = ray_coeff_.S[2] * A[ray_coeff_.k[2]];
    const T Bz = ray_coeff_.S[2] * B[ray_coeff_.k[2]];
    const T Cz = ray_coeff_.S[2] * C[ray_coeff_.k[2]];
    const T D = U * Az + V * Bz + W * Cz;

    const T rcpDet = static_cast<T>(1.0) / det;
    T tt = D * rcpDet;

    if (tt > (*t_inout)) {
      return false;
    }

    if (tt < t_min_) {
      return false;
    }

    (*t_inout) = tt;
    /**
     * TODO: Citation needed.
     * Use Möller-Trumbore style barycentric coordinates
     * U + V + W = 1.0 and interp(p) = U * p0 + V * p1 + W * p2
     * We want interp(p) = (1 - u - v) * p0 + u * v1 + v * p2;
     * => u = V, v = W.
     */
    uv_[0] = V * rcpDet;
    uv_[1] = W * rcpDet;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

    return true;
  }
};

}// namespace blazert

#endif// BLAZERT_H_
