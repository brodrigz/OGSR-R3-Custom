#include "common.h"

struct a2v
{
    // 	float4 tc0:		TEXCOORD0;	// Texture coordinates
    float4 P : POSITION; // Object-space position
};

//////////////////////////////////////////////////////////////////////////////////////////
// Vertex
#ifdef USE_RSM
v2p_shadow_direct main(v_static I)
#else
v2p_shadow_direct main(a2v I)
#endif
{
    v2p_shadow_direct O;

    O.hpos = mul(m_WVP, I.P);
#ifdef USE_RSM
    O.tc0 = unpack_tc_base(I.tc, I.T.w, I.B.w);
    O.rsm_world = mul(m_W, I.P);
#endif
#ifndef USE_HWSMAP
    O.depth = O.hpos.z;
#endif
    return O;
}
FXVS;
