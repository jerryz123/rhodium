// Checks repeated CHI builders and complete Home transforms, including optional
// metadata at every DAT width.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
#include <bit>
#include <random>

template <std::size_t W, std::size_t N>
rhodium_rsim::WideBits<W> prefix(const rhodium_rsim::WideBits<N> &value) {
  rhodium_rsim::WideBits<W> result;
  std::copy_n(value.words.begin(), result.words.size(), result.words.begin());
  return result;
}
template <class T> T inverted_response(T value) {
  value.pcache_uline_uid ^= 63;
  value.ptrace_utag ^= 1;
  value.ptag_uop ^= 3;
  value.ppcrd_utype ^= 15;
  value.pdbid_uor_ugroup_uid ^= 4095;
  value.pc_ubusy ^= 7;
  value.pfwd_ustate_uor_udata_upull ^= 7;
  value.presp ^= 7;
  value.presp_uerr ^= 3;
  value.popcode ^= 31;
  value.ptxn_uid ^= 4095;
  value.psrc_uid ^= 65535;
  value.ptgt_uid ^= 65535;
  value.pqos ^= 15;
  return value;
}

std::uint64_t expected_mask(int bytes_per_packet, int address, int size) {
  std::uint64_t return_value{};

  std::uint64_t mask;
  mask = {};
  for (int b = 0; b < bytes_per_packet; b++)
    if ((1 << size) >= bytes_per_packet ||
        (b >= address % bytes_per_packet &&
         b < (address % bytes_per_packet) + (1 << size)))
      mask |= UINT64_C(1) << b;
  return mask;

  return return_value;
}

#define CHECK_RSP(PORT, OPCODE, REQ, NODE, DBID)                               \
  CHECK(PORT.popcode == OPCODE && PORT.ptxn_uid == REQ.ptxn_uid &&             \
        PORT.psrc_uid == NODE && PORT.ptgt_uid == REQ.psrc_uid &&              \
        PORT.pdbid_uor_ugroup_uid == DBID && PORT.pqos == REQ.pqos);           \
  CHECK(PORT.pcache_uline_uid == 0 && PORT.ptrace_utag == 0 &&                 \
        PORT.ptag_uop == 0 && PORT.ppcrd_utype == 0 && PORT.pc_ubusy == 0 &&   \
        PORT.pfwd_ustate_uor_udata_upull == 0 && PORT.presp == 0 &&            \
        PORT.presp_uerr == 0);

#define CHECK_DAT(PORT, WIDTH, REQ, NODE, ID, DATA)                            \
  CHECK(PORT.popcode == UINT64_C(4) &&                                         \
        PORT.ptxn_uid == REQ.preturn_utxn_uid_uor_ustash_ulpid &&              \
        PORT.psrc_uid == NODE &&                                               \
        PORT.ptgt_uid == REQ.preturn_unid_uor_ustash_unid_uor_udata_utarget && \
        PORT.phome_unid_uor_upbha_uor_umismatched_umecid == NODE &&            \
        PORT.pqos == REQ.pqos && PORT.pdata_uid == ID &&                       \
        PORT.pdata == prefix<WIDTH>(DATA));                                    \
  CHECK(((PORT.pbyte_uenable) & low_mask(64)) ==                               \
        expected_mask(WIDTH / 8, addr, sz));                                   \
  CHECK(PORT.preplicate == 0 && PORT.pnum_udat == 0 && PORT.pcah == 0 &&       \
        PORT.ptrace_utag == 0 && PORT.ptag_uupdate == 0 && PORT.ptag == 0 &&   \
        PORT.ptag_uop == 0 && PORT.pcache_uline_uid == 0 && PORT.pccid == 0 && \
        PORT.pdbid_uor_umecid == 0 && PORT.pc_ubusy == 0 &&                    \
        PORT.pdata_upull == 0 && PORT.pdata_usource_uor_ufwd_ustate == 0 &&    \
        PORT.presp == 0 && PORT.presp_uerr == 0);

#define CHECK_HNI(P, REQ_PORT, RSP_PORT, WRITE_PORT, READ_PORT, ORIGINAL_REQ,  \
                  ORIGINAL_RSP, HOME, TARGET, SLOT, TXN, QOS, WRITE_HOME,      \
                  DATAID)                                                      \
  {                                                                            \
    std::remove_cvref_t<decltype(original_req_##P)> expected_req;              \
    std::remove_cvref_t<decltype(original_rsp_##P)> expected_rsp;              \
    std::remove_cvref_t<decltype(original_dat_##P)> expected_dat;              \
    expected_req = ORIGINAL_REQ;                                               \
    expected_req.pexp_ucomp_uack = 0;                                          \
    expected_req.psnp_uattr_uor_udo_udwt = 0;                                  \
    expected_req.ppcrd_utype = 0;                                              \
    expected_req.porder = 0;                                                   \
    expected_req.pallow_uretry = 0;                                            \
    expected_req.pmulti_ureq = 0;                                              \
    expected_req.preturn_utxn_uid_uor_ustash_ulpid = ((SLOT) & low_mask(12));  \
    expected_req.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint = 0;    \
    expected_req.preturn_unid_uor_ustash_unid_uor_udata_utarget = HOME;        \
    expected_req.ptxn_uid = ((SLOT) & low_mask(12));                           \
    expected_req.psrc_uid = HOME;                                              \
    expected_req.ptgt_uid = TARGET;                                            \
    CHECK(same_request(REQ_PORT, expected_req));                               \
    expected_rsp = ORIGINAL_RSP;                                               \
    expected_rsp.ppcrd_utype = 0;                                              \
    expected_rsp.pdbid_uor_ugroup_uid = ((SLOT) & low_mask(12));               \
    expected_rsp.ptxn_uid = TXN;                                               \
    expected_rsp.psrc_uid = HOME;                                              \
    expected_rsp.ptgt_uid = TARGET;                                            \
    expected_rsp.pqos = QOS;                                                   \
    CHECK(same_response(RSP_PORT, expected_rsp));                              \
    expected_dat = original_dat_##P;                                           \
    expected_dat.pdata_uid = DATAID;                                           \
    expected_dat.phome_unid_uor_upbha_uor_umismatched_umecid = WRITE_HOME;     \
    expected_dat.ptxn_uid = TXN;                                               \
    expected_dat.psrc_uid = WRITE_HOME;                                        \
    expected_dat.ptgt_uid = TARGET;                                            \
    expected_dat.pqos = QOS;                                                   \
    CHECK(same_data(WRITE_PORT, expected_dat));                                \
    expected_dat = original_dat_##P;                                           \
    expected_dat.phome_unid_uor_upbha_uor_umismatched_umecid = HOME;           \
    expected_dat.ptxn_uid = TXN;                                               \
    expected_dat.psrc_uid = HOME;                                              \
    expected_dat.ptgt_uid = TARGET;                                            \
    expected_dat.pqos = QOS;                                                   \
    CHECK(same_data(READ_PORT, expected_dat));                                 \
  }

#define CHECK_HOME(P)                                                          \
  {                                                                            \
    std::remove_cvref_t<decltype(original_req_##P)> expected_req;              \
    std::remove_cvref_t<decltype(original_dat_##P)> expected_dat;              \
    std::remove_cvref_t<decltype(snoop_##P)> expected_snp;                     \
    expected_req = original_req_##P;                                           \
    expected_req.pexp_ucomp_uack = 0;                                          \
    expected_req.pexcl_usnoop_ume_ucah = 0;                                    \
    expected_req.psnp_uattr_uor_udo_udwt = 0;                                  \
    expected_req.pmem_uattr.pearly_uwrite_uacknowledge =                       \
        other_request.pmem_uattr.pearly_uwrite_uacknowledge;                   \
    expected_req.ppcrd_utype = 0;                                              \
    expected_req.porder = 0;                                                   \
    expected_req.pallow_uretry = 0;                                            \
    expected_req.paddress = other_request.paddress;                            \
    expected_req.psize_uor_unum_ureq = other_request.psize_uor_unum_ureq;      \
    expected_req.pmulti_ureq = 0;                                              \
    expected_req.popcode = other_request.popcode;                              \
    expected_req.preturn_utxn_uid_uor_ustash_ulpid = other_request.ptxn_uid;   \
    expected_req.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint = 0;    \
    expected_req.preturn_unid_uor_ustash_unid_uor_udata_utarget = node_id;     \
    expected_req.ptxn_uid = other_request.ptxn_uid;                            \
    expected_req.psrc_uid = node_id;                                           \
    expected_req.ptgt_uid = other_node_id;                                     \
    CHECK(same_request(downstream_##P, expected_req));                         \
    expected_dat = original_dat_##P;                                           \
    expected_dat.preplicate = 0;                                               \
    expected_dat.pnum_udat = 0;                                                \
    expected_dat.pcah = 0;                                                     \
    expected_dat.pdbid_uor_umecid = other_request.ptxn_uid;                    \
    expected_dat.pc_ubusy = 0;                                                 \
    expected_dat.pdata_upull = 0;                                              \
    expected_dat.pdata_usource_uor_ufwd_ustate = 0;                            \
    expected_dat.presp = 0;                                                    \
    expected_dat.popcode = UINT64_C(3);                                        \
    expected_dat.phome_unid_uor_upbha_uor_umismatched_umecid = node_id;        \
    expected_dat.ptxn_uid = other_request.ptxn_uid;                            \
    expected_dat.psrc_uid = node_id;                                           \
    expected_dat.ptgt_uid = other_node_id;                                     \
    CHECK(same_data(write_##P, expected_dat));                                 \
    expected_dat = original_dat_##P;                                           \
    expected_dat.pdbid_uor_umecid = 0;                                         \
    expected_dat.pdata_upull = 0;                                              \
    expected_dat.pdata_usource_uor_ufwd_ustate = 0;                            \
    expected_dat.presp = (other_data.words[0] & 7);                            \
    expected_dat.popcode = UINT64_C(4);                                        \
    expected_dat.phome_unid_uor_upbha_uor_umismatched_umecid = node_id;        \
    expected_dat.ptxn_uid =                                                    \
        original_req_##P.preturn_utxn_uid_uor_ustash_ulpid;                    \
    expected_dat.psrc_uid = node_id;                                           \
    expected_dat.ptgt_uid =                                                    \
        original_req_##P.preturn_unid_uor_ustash_unid_uor_udata_utarget;       \
    expected_dat.pqos = original_req_##P.pqos;                                 \
    CHECK(same_data(upstream_##P, expected_dat));                              \
    expected_dat = {};                                                         \
    expected_dat.pdata = original_dat_##P.pdata;                               \
    expected_dat.pbyte_uenable = original_dat_##P.pbyte_uenable;               \
    expected_dat.pdata_uid = original_dat_##P.pdata_uid;                       \
    expected_dat.pccid = original_dat_##P.pccid;                               \
    expected_dat.pdbid_uor_umecid = original_dat_##P.pdbid_uor_umecid;         \
    expected_dat.popcode = UINT64_C(3);                                        \
    expected_dat.phome_unid_uor_upbha_uor_umismatched_umecid = other_node_id;  \
    expected_dat.ptxn_uid = other_request.ptxn_uid;                            \
    expected_dat.psrc_uid = node_id;                                           \
    expected_dat.ptgt_uid = other_node_id;                                     \
    CHECK(same_data(requester_write_##P, expected_dat));                       \
    expected_snp = {};                                                         \
    expected_snp.ptrace_utag = original_req_##P.ptrace_utag;                   \
    expected_snp.ppas = original_req_##P.ppas;                                 \
    expected_snp.paddress = slice(other_request.paddress, 51, 3);              \
    expected_snp.popcode = UINT64_C(7);                                        \
    expected_snp.ptxn_uid = other_request.ptxn_uid;                            \
    expected_snp.psrc_uid = node_id;                                           \
    expected_snp.pqos = original_req_##P.pqos;                                 \
    CHECK(same_snoop(snoop_##P, expected_snp));                                \
    expected_req = {};                                                         \
    expected_req.ptrace_utag = original_dat_##P.ptrace_utag;                   \
    expected_req.pmem_uattr = original_req_##P.pmem_uattr;                     \
    expected_req.pmem_uattr.pearly_uwrite_uacknowledge =                       \
        other_request.pmem_uattr.pearly_uwrite_uacknowledge;                   \
    expected_req.ppas = original_req_##P.ppas;                                 \
    expected_req.paddress = ((original_req_##P.paddress & ~UINT64_C(63)) |     \
                             (original_dat_##P.pdata_uid << 4));               \
    expected_req.psize_uor_unum_ureq =                                         \
        std::countr_zero(unsigned(original_dat_##P.pdata.words.size() * 4));   \
    expected_req.popcode = UINT64_C(29);                                       \
    expected_req.preturn_unid_uor_ustash_unid_uor_udata_utarget = node_id;     \
    expected_req.ptxn_uid = other_request.ptxn_uid;                            \
    expected_req.psrc_uid = node_id;                                           \
    expected_req.ptgt_uid = other_node_id;                                     \
    expected_req.pqos = original_req_##P.pqos;                                 \
    CHECK(same_request(intervention_##P, expected_req));                       \
  }

void run_case() {
  std::mt19937 random(0x434849);
  std::remove_cvref_t<decltype(home_response)> expected_home_response;
  for (int sz = 0; sz <= 6; sz++) {
    for (int addr = 0; addr < 256; addr += (1 << sz)) {
      fill_request(request, random, 16, 52);
      request.paddress = addr;
      request.psize_uor_unum_ureq = sz;
      request.pmulti_ureq = 0;
      node_id = random() & 0xffff;
      dbid = random() & 31;
      data_id = random() & 3;
      for (auto &word : data.words)
        word = random();
      fill_request(other_request, random, 16, 52);
      other_request.paddress = request.paddress;
      other_request.psize_uor_unum_ureq = request.psize_uor_unum_ureq;
      other_request.pmulti_ureq = 0;
      other_node_id = node_id ^ 0xffff;
      other_dbid = dbid ^ 31;
      other_data_id = data_id ^ 3;
      other_data = ~prefix<128>(data);
      for (auto &word : home_request_bits.words)
        word = random();
      for (auto &word : home_data_bits.words)
        word = random();
      eval();
      expected_home_response = {};
      expected_home_response.popcode = UINT64_C(4);
      expected_home_response.ptxn_uid = request.ptxn_uid;
      expected_home_response.psrc_uid = node_id;
      expected_home_response.ptgt_uid = request.psrc_uid;
      expected_home_response.pqos = request.pqos;
      expected_home_response.presp_uerr = (other_data.words[0] & 3);
      CHECK(same_response(home_response, expected_home_response));
      CHECK_HNI(w128, hni_req_w128, hni_rsp_w128, hni_write_w128, hni_read_w128,
                original_req_w128, original_rsp_w128, UINT64_C(37),
                other_node_id, dbid, other_request.ptxn_uid, other_request.pqos,
                node_id, other_data_id);
      CHECK_HNI(h128, hni_req_h128, hni_rsp_h128, hni_write_h128, hni_read_h128,
                original_req_h128, original_rsp_h128, UINT64_C(37),
                other_node_id, dbid, other_request.ptxn_uid, other_request.pqos,
                node_id, other_data_id);
      CHECK_HNI(w256, hni_req_w256, hni_rsp_w256, hni_write_w256, hni_read_w256,
                original_req_w256, original_rsp_w256, UINT64_C(37),
                other_node_id, dbid, other_request.ptxn_uid, other_request.pqos,
                node_id, other_data_id);
      CHECK_HNI(h256, hni_req_h256, hni_rsp_h256, hni_write_h256, hni_read_h256,
                original_req_h256, original_rsp_h256, UINT64_C(37),
                other_node_id, dbid, other_request.ptxn_uid, other_request.pqos,
                node_id, other_data_id);
      CHECK_HNI(w512, hni_req_w512, hni_rsp_w512, hni_write_w512, hni_read_w512,
                original_req_w512, original_rsp_w512, UINT64_C(37),
                other_node_id, dbid, other_request.ptxn_uid, other_request.pqos,
                node_id, other_data_id);
      CHECK_HNI(h512, hni_req_h512, hni_rsp_h512, hni_write_h512, hni_read_h512,
                original_req_h512, original_rsp_h512, UINT64_C(37),
                other_node_id, dbid, other_request.ptxn_uid, other_request.pqos,
                node_id, other_data_id);
      CHECK_HNI(w128, other_hni_req, other_hni_rsp, other_hni_write,
                other_hni_read, other_request,
                inverted_response(original_rsp_w128), UINT64_C(42), node_id,
                other_dbid, request.ptxn_uid, request.pqos, other_node_id,
                data_id);
      CHECK_HOME(w128);
      CHECK_HOME(h128);
      CHECK_HOME(w256);
      CHECK_HOME(h256);
      CHECK_HOME(w512);
      CHECK_HOME(h512);
      CHECK_RSP(dbid_response, UINT64_C(6), request, node_id, dbid);
      CHECK_RSP(write_response, UINT64_C(4), request, node_id, dbid);
      CHECK_RSP(other_dbid_response, UINT64_C(6), other_request, other_node_id,
                other_dbid);
      CHECK_RSP(other_write_response, UINT64_C(4), other_request, other_node_id,
                other_dbid);
      CHECK_DAT(other_read_response, 128, other_request, other_node_id,
                other_data_id, other_data);
      CHECK_DAT(read_w128, 128, request, node_id, data_id, data);
      CHECK_DAT(read_o128, 128, request, node_id, data_id, data);
      CHECK_DAT(read_w256, 256, request, node_id, data_id, data);
      CHECK_DAT(read_o256, 256, request, node_id, data_id, data);
      CHECK_DAT(read_w512, 512, request, node_id, data_id, data);
      CHECK_DAT(read_o512, 512, request, node_id, data_id, data);
      CHECK(read_o128.ppoison == 0 && read_o128.pdata_ucheck == 0 &&
            read_o128.prsvdc == 0 && read_o256.ppoison == 0 &&
            read_o256.pdata_ucheck == 0 && read_o256.prsvdc == 0 &&
            read_o512.ppoison == 0 && read_o512.pdata_ucheck == 0 &&
            read_o512.prsvdc == 0);
    }
  }
}
#undef CHECK_RSP
#undef CHECK_DAT
#undef CHECK_HNI
#undef CHECK_HOME

int main() { return run_test(run_case); }
