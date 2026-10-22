#ifndef RUNEC_VIEWER_ACTOR_PROJECTION_H
#define RUNEC_VIEWER_ACTOR_PROJECTION_H

float runec_actor_model_midpoint(float min_y, float max_y, float scale);
int runec_actor_model_vertical_bounds(const float *vertices, int vertex_count,
                                      float *min_y, float *max_y);

#endif
