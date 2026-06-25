#include "crypt/milenage_adapter.hpp"
#include "crypt/milenage.hpp"

#include <algorithm>

namespace ims::crypt {

auto compute_opc(std::span<const uint8_t, 16> k,
                 std::span<const uint8_t, 16> op) -> Result<std::array<uint8_t, 16>> {
    std::array<uint8_t, 16> opc{};
    ComputeOPc(const_cast<BYTE*>(op.data()),
               const_cast<BYTE*>(k.data()),
               opc.data());
    return opc;
}

auto generate_vector(std::span<const uint8_t, 16> opc,
                     std::span<const uint8_t, 16> k,
                     std::span<const uint8_t, 6> sqn,
                     std::span<const uint8_t, 2> amf,
                     std::span<const uint8_t, 16> rand,
                     bool use_ak) -> Result<MilenageVector> {
    MilenageVector vector;

    // f2345: compute RES, CK, IK, AK
    f2345(const_cast<BYTE*>(opc.data()),
          const_cast<BYTE*>(k.data()),
          const_cast<BYTE*>(rand.data()),
          vector.xres.data(),
          vector.ck.data(),
          vector.ik.data(),
          vector.ak.data());

    // f1: compute MAC-A
    std::array<uint8_t, 8> mac_a{};
    f1(const_cast<BYTE*>(opc.data()),
       const_cast<BYTE*>(k.data()),
       const_cast<BYTE*>(rand.data()),
       const_cast<BYTE*>(sqn.data()),
       const_cast<BYTE*>(amf.data()),
       mac_a.data());

    // Build AUTN: (SQN [^ AK]) || AMF || MAC-A.
    // MAC-A (f1 above) is always over the cleartext SQN; only the SQN field
    // carried in AUTN is optionally concealed with AK. UEs provisioned with
    // eak=false do not de-conceal SQN, so the network must send it in clear.
    for (std::size_t i = 0; i < 6; ++i) {
        vector.autn[i] = use_ak ? static_cast<uint8_t>(sqn[i] ^ vector.ak[i]) : sqn[i];
    }
    vector.autn[6] = amf[0];
    vector.autn[7] = amf[1];
    std::copy(mac_a.begin(), mac_a.end(), vector.autn.begin() + 8);

    return vector;
}

} // namespace ims::crypt
