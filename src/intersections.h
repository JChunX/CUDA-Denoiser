#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>

#include "sceneStructs.h"
#include "utilities.h"

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

// CHECKITOUT
/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t) {
    return r.origin + (t - .0001f) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v) {
    return glm::vec3(m * v);
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed cube. Untransformed,
 * the cube ranges from -0.5 to 0.5 in each axis and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float boxIntersectionTest(Geom box, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    Ray q;
    q.origin    =                multiplyMV(box.inverseTransform, glm::vec4(r.origin   , 1.0f));
    q.direction = glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)));

    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = q.direction[xyz];
        /*if (glm::abs(qdxyz) > 0.00001f)*/ {
            float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
            float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                tmax_n = n;
            }
        }
    }

    if (tmax >= tmin && tmax > 0) {
        outside = true;
        if (tmin <= 0) {
            tmin = tmax;
            tmin_n = tmax_n;
            outside = false;
        }
        intersectionPoint = multiplyMV(box.transform, glm::vec4(getPointOnRay(q, tmin), 1.0f));
        normal = glm::normalize(multiplyMV(box.invTranspose, glm::vec4(tmin_n, 0.0f)));
        return glm::length(r.origin - intersectionPoint);
    }
    return -1;
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed sphere. Untransformed,
 * the sphere always has radius 0.5 and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float sphereIntersectionTest(Geom sphere, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    float radius = .5;

    glm::vec3 ro = multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 rd = glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)));

    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float vDotDirection = glm::dot(rt.origin, rt.direction);
    float radicand = vDotDirection * vDotDirection - (glm::dot(rt.origin, rt.origin) - powf(radius, 2));
    if (radicand < 0) {
        return -1;
    }

    float squareRoot = sqrt(radicand);
    float firstTerm = -vDotDirection;
    float t1 = firstTerm + squareRoot;
    float t2 = firstTerm - squareRoot;

    float t = 0;
    if (t1 < 0 && t2 < 0) {
        return -1;
    } else if (t1 > 0 && t2 > 0) {
        t = min(t1, t2);
        outside = true;
    } else {
        t = max(t1, t2);
        outside = false;
    }

    glm::vec3 objspaceIntersection = getPointOnRay(rt, t);

    intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
    normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));
    if (!outside) {
        normal = -normal;
    }

    return glm::length(r.origin - intersectionPoint);
}

// CHECKITOUT
/**
 * Test intersection between a ray and a mesh.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float meshIntersectionTest(
    Mesh mesh, Ray r, 
    glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside
) {
    glm::vec3 const ro = multiplyMV(mesh.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 const rd = glm::normalize(multiplyMV(mesh.inverseTransform, glm::vec4(r.direction, 0.0f)));
    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float t = -1;
    for (int i = 0; i < mesh.numIndices; i += 3) {
        glm::vec3 const vert0 = glm::vec3(
            mesh.vertices[mesh.indices[i]*3],
            mesh.vertices[mesh.indices[i]*3 + 1],
            mesh.vertices[mesh.indices[i]*3 + 2]
        );
        glm::vec3 const vert1 = glm::vec3(
            mesh.vertices[mesh.indices[i + 1]*3],
            mesh.vertices[mesh.indices[i + 1]*3 + 1],
            mesh.vertices[mesh.indices[i + 1]*3 + 2]
        );
        glm::vec3 const vert2 = glm::vec3(
            mesh.vertices[mesh.indices[i + 2]*3],
            mesh.vertices[mesh.indices[i + 2]*3 + 1],
            mesh.vertices[mesh.indices[i + 2]*3 + 2]
        );

        glm::vec3 barycentric;
        if (glm::intersectRayTriangle(ro, rd, vert0, vert1, vert2, barycentric)) {
            glm::vec3 intersection = vert0 + 
                barycentric.x * (vert1 - vert0) + 
                barycentric.y * (vert2 - vert0);

            float current_t = glm::length(intersection - rt.origin);
            if (t < 0 || current_t < t) {
                t = current_t;
                intersectionPoint = intersection;
                normal = glm::normalize(glm::cross(vert1 - vert0, vert2 - vert0));
            }
        }
    }

    if (t < 0) {
        return -1;
    }

    intersectionPoint = multiplyMV(mesh.transform, glm::vec4(intersectionPoint, 1.f));
    normal = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(normal, 0.f)));
    outside = glm::dot(normal, rt.direction) < 0;
    if (!outside) {
        normal = -normal;
    }

    return glm::length(r.origin - intersectionPoint);
}


__host__ __device__ float octreeIntersectionTest(
    OctreeDev octree, Ray r, 
    glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside
) {
    glm::mat4 const inverseTransform = octree.inverseTransform;
    glm::vec3 const ro = multiplyMV(inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 const rd = glm::normalize(multiplyMV(inverseTransform, glm::vec4(r.direction, 0.0f)));
    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float t = -1;
    int stack[8*8*8]; // TODO: determine max stack size
    int stackSize = 0;
    stack[stackSize] = octree.root;
    stackSize++;
    while (stackSize > 0) {
        int nodeIndex = stack[stackSize-1];
        stackSize--;
        OctreeNode node = octree.nodes[nodeIndex];
        Geom boundingBox = octree.boundingBoxes[nodeIndex];

        glm::vec3 tmp_intersect;
        glm::vec3 tmp_normal;

        float box_intersect = boxIntersectionTest(boundingBox, r, tmp_intersect, tmp_normal, outside);

        if (box_intersect > 0) {

            if (node.isLeaf) {
                for (int i = octree.dataStarts[nodeIndex]; i < octree.dataStarts[nodeIndex + 1]; i++) {
                    
                    glm::vec3 const vert0 = glm::vec3(
                        octree.triangles[i].vertices[0].x,
                        octree.triangles[i].vertices[0].y,
                        octree.triangles[i].vertices[0].z
                    );
                    glm::vec3 const vert1 = glm::vec3(
                        octree.triangles[i].vertices[1].x,
                        octree.triangles[i].vertices[1].y,
                        octree.triangles[i].vertices[1].z
                    );
                    glm::vec3 const vert2 = glm::vec3(
                        octree.triangles[i].vertices[2].x,
                        octree.triangles[i].vertices[2].y,
                        octree.triangles[i].vertices[2].z
                    );

                    glm::vec3 barycentric;
                    if (glm::intersectRayTriangle(ro, rd, vert0, vert1, vert2, barycentric)) {
                        glm::vec3 intersection = vert0 + 
                            barycentric.x * (vert1 - vert0) + 
                            barycentric.y * (vert2 - vert0);

                        float current_t = glm::length(intersection - rt.origin);
                        if (t < 0 || current_t < t) {
                            t = current_t;
                            intersectionPoint = intersection;
                            normal = glm::normalize(glm::cross(vert1 - vert0, vert2 - vert0));
                        }
                    }

                }
            }
            else {
                for (int i = 0; i < 8; i++) {
                    if (node.children[i] != -1) {
                        stack[stackSize] = node.children[i];
                        stackSize++;
                    }
                }
            }

        }
    }

    if (t < 0) {
        return -1;
    }


    glm::mat4 transform = octree.transform;
    glm::mat4 invTranspose = octree.invTranspose;

    intersectionPoint = multiplyMV(transform, glm::vec4(intersectionPoint, 1.f));
    normal = glm::normalize(multiplyMV(invTranspose, glm::vec4(normal, 0.f)));
    outside = glm::dot(normal, rt.direction) < 0;
    if (!outside) {
        normal = -normal;
    }
    return glm::length(r.origin - intersectionPoint);


}