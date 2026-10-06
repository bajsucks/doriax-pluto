// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef TRANSLATETRACKS_H
#define TRANSLATETRACKS_H

#include "KeyframeTracks.h"

namespace doriax{
    class DORIAX_API TranslateTracks: public KeyframeTracks{

    public:
        TranslateTracks(Scene* scene);
        TranslateTracks(Scene* scene, Entity entity);
        TranslateTracks(Scene* scene, std::vector<float> times, std::vector<Vector3> values);

        void setValues(std::vector<Vector3> values);
    };
}

#endif //TRANSLATETRACKS_H