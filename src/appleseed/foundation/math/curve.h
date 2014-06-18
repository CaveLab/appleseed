
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2014 Srinath Ravichandran, The appleseedhq Organization
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef APPLESEED_FOUNDATION_MATH_CURVE_H
#define APPLESEED_FOUNDATION_MATH_CURVE_H

// appleseed.foundation headers.
#include "foundation/math/aabb.h"
#include "foundation/math/bezier.h"
#include "foundation/math/matrix.h"
#include "foundation/math/ray.h"
#include "foundation/math/scalar.h"
#include "foundation/math/vector.h"

// Standard headers.
#include <cstddef>
#include <cmath>
#include <limits>

namespace foundation
{

// Generic function that can be used to compute the transform matrix required for ray-curve intersection.
// Since a ray will be tested against multiple curves, it would be good to compute it once and use it everywhere else.
// Templated over type(T), with MatrixType(M) and RayType(R)
template <typename T>
Matrix<T, 4, 4> ray_curve_intersection_xfm(const Ray<T, 3>& r)
{
    typedef T ValueType;
    typedef Matrix<T, 4, 4> MatrixType;
    typedef Vector<T, 3> VectorType;

    MatrixType rotate;

    const VectorType rdir = normalize(r.m_dir);
    const ValueType d = std::sqrt(rdir.x * rdir.x + rdir.z * rdir.z);

    // We are actually required to do a test against 0 but we consider a small epsilon for our calculations.
    if (d >= ValueType(1.0e-6))
    {
        const ValueType inv_d = ValueType(1.0) / d;
        rotate(0, 0) = rdir.z * inv_d;             rotate(0, 1) = ValueType(0.0);   rotate(0, 2) = -rdir.x * inv_d;            rotate(0, 3) = ValueType(0.0);
        rotate(1, 0) = -(rdir.x * rdir.y) * inv_d; rotate(1, 1) = d;                rotate(1, 2) = -(rdir.y * rdir.z) * inv_d; rotate(1, 3) = ValueType(0.0);
        rotate(2, 0) = rdir.x;                     rotate(2, 1) = rdir.y;           rotate(2, 2) = rdir.z;                     rotate(2, 3) = ValueType(0.0);
        rotate(3, 0) = ValueType(0.0);             rotate(3, 1) = ValueType(0.0);   rotate(3, 2) = ValueType(0.0);             rotate(3, 3) = ValueType(1.0);
    }
    else
    {
        // We replace the matrix by the one that rotates about the x axis by Pi/2.
        // The sign of rotation depends on the sign of the y component of the direction vector.
        const ValueType angle = rdir.y > ValueType(0.0) ? ValueType(HalfPi) : -ValueType(HalfPi);
        rotate = MatrixType::rotation_x(angle);
    }

    return rotate * MatrixType::translation(-r.m_org);
}


//
// The Curve class wraps a Bezier curve of degree N and adds an intersecion method.
//

template <typename BezierType>
class Curve
{
  public:
    typedef typename BezierType::ValueType ValueType;
    typedef Ray<ValueType, 3> RayType;
    typedef Matrix<ValueType, 4, 4> MatrixType;
    typedef AABB<ValueType, 3> AABBType;
    typedef Vector<ValueType, 3> VectorType;

    Curve(const BezierType& bezier)
      : m_bezier(bezier)
    {
    }

    bool intersect(const RayType& ray, const MatrixType& xfm, ValueType& t) const
    {
        const BezierType xfm_bezier(m_bezier, xfm);
        const size_t depth = xfm_bezier.compute_max_recursion_depth();

        ValueType hit;
        ValueType phit = std::numeric_limits<ValueType>::max();
        return converge(depth, xfm_bezier, xfm, 0, 1, hit, phit);
    }

    size_t get_control_point_count() const
    {
        return m_bezier.get_control_point_count();
    }

    VectorType get_control_point(const size_t index) const
    {
        return m_bezier.get_control_point(index);
    }

    AABBType get_bounds() const
    {
        return m_bezier.get_bounds();
    }

  private:
    const BezierType& m_bezier;

    bool converge(
        const size_t        depth, 
        const BezierType&   bezier, 
        const Matrix4f&     xfm, 
        const ValueType     v0, 
        const ValueType     vn, 
        ValueType&          hit, 
        ValueType&          phit) const
    {
        const ValueType curve_width = bezier.get_max_width() * ValueType(0.5);

        const AABBType& bbox = bezier.get_bounds();

        if (bbox.min.z >= phit        || bbox.max.z <= ValueType(1e-6) ||
            bbox.min.x >= curve_width || bbox.max.x <= -curve_width    ||
            bbox.min.y >= curve_width || bbox.max.y <= -curve_width)
            return false;

        if (depth == 0)
        {
            // Compute the intersection.
            static const size_t Degree = BezierType::Degree;
            const VectorType dir = bezier.get_control_point(Degree) - bezier.get_control_point(0);

            VectorType dp0 = bezier.get_control_point(1) - bezier.get_control_point(0);
            if (dot(dir, dp0) < ValueType(0.0))
                dp0 = -dp0;

            if (dot(dp0, bezier.get_control_point(0)) > ValueType(0.0))
                return false;

            VectorType dpn = bezier.get_control_point(Degree) - bezier.get_control_point(Degree - 1);
            if (dot(dir, dpn) < ValueType(0.0))
                dpn = -dpn;

            if (dot(dpn, bezier.get_control_point(Degree)) < ValueType(0.0))
                return false;

            // Compute w on the line segment.
            ValueType w = dir.x * dir.x + dir.y * dir.y;
            if (w < ValueType(1.0e-6))
                return false;
            w = -(bezier.get_control_point(0).x * dir.x + bezier.get_control_point(0).y * dir.y) / w;
            w = saturate(w);

            // Compute v on the line segment.
            const ValueType v = v0 * (ValueType(1.0) - w) + vn * w;

            // Compute point on original unsplit curve.
            const VectorType orig_p = m_bezier.evaluate_point(v);

            // Transform back to orignal required frame.
            const VectorType p = BezierType::transform_point(xfm, orig_p);

            if (p.z <= ValueType(1.0e-6) || phit < p.z)
                return false;

            // Compute the correct interpolated width on the transformed curve and not original curve.
            // Note: We use the modified curve and not the actual curve because the width values are
            // correctly split and interpolated during split operations. In order to have a smooth
            // transition between the control point widths, we use the transformed curve.
            const ValueType half_width = ValueType(0.5) * bezier.evaluate_width(w);

            if (p.x * p.x + p.y * p.y >= half_width * half_width)
                return false;

            // Found an intersection.
            phit = p.z;
            hit  = v;
            return true;
        }
        else
        {
            // Split the curve and recurse on the two new curves.

            BezierType b1, b2;
            bezier.split(b1, b2);

            const ValueType vm = (v0 + vn) * ValueType(0.5);
            
            ValueType v_left, v_right;
            ValueType t_left = std::numeric_limits<ValueType>::max();
            ValueType t_right = std::numeric_limits<ValueType>::max();
            const bool hit_left  = converge(depth - 1, b1, xfm, v0, vm, v_left, t_left);
            const bool hit_right = converge(depth - 1, b2, xfm, vm, vn, v_right, t_right);

            if (hit_left || hit_right)
            {
                if (t_left < t_right)
                {
                    hit = v_left;
                    phit = t_left;
                }
                else
                {
                    hit = v_right;
                    phit = t_right;
                }
                return true;
            }

            return false;
        }
    }
};


//
// Full specializations for degree 1, 2, 3 Bezier curves of type float and double.
//

typedef Curve<Bezier1f> Curve1f;
typedef Curve<Bezier1d> Curve1d;
typedef Curve<Bezier2f> Curve2f;
typedef Curve<Bezier2d> Curve2d;
typedef Curve<Bezier3f> Curve3f;
typedef Curve<Bezier3d> Curve3d;

}       // namespace foundation

#endif  // !APPLESEED_FOUNDATION_MATH_CURVE_H
