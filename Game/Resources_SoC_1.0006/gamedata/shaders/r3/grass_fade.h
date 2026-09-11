#ifndef GRASS_FADE_H
#define GRASS_FADE_H

// Spatially distributed thresholds instead of a repeating 4x4 mask. Use the
// actual raster position (also correct for shadow-map viewports); never vary
// the seed by frame, so temporal reconstruction does not see animated noise.
// No texture reads, additional passes or changes to fully visible grass.
void grass_fade(float alpha, float2 raster_position)
{
    float threshold = frac(52.9829189 * frac(dot(floor(raster_position), float2(0.06711056, 0.00583715))));
    clip(alpha - max(threshold, 1.0 / 65536.0));
}

#endif
