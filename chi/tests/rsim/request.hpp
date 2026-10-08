// Compares every CHI request field and constructs deterministic metadata for
// transparency tests.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"
#include <tuple>
template <class T> auto request_fields(const T &value) {
  return std::tie(value.ptrace_utag, value.ptag_uop, value.pexp_ucomp_uack,
                  value.pexcl_usnoop_ume_ucah, value.pgroup_uid_uor_ulpid,
                  value.psnp_uattr_uor_udo_udwt, value.pmem_uattr.pallocate,
                  value.pmem_uattr.pcacheable, value.pmem_uattr.pdevice,
                  value.pmem_uattr.pearly_uwrite_uacknowledge,
                  value.ppcrd_utype, value.porder, value.pallow_uretry,
                  value.plikely_ushared, value.ppas, value.paddress,
                  value.psize_uor_unum_ureq, value.pmulti_ureq, value.popcode,
                  value.preturn_utxn_uid_uor_ustash_ulpid,
                  value.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint,
                  value.preturn_unid_uor_ustash_unid_uor_udata_utarget,
                  value.ptxn_uid, value.psrc_uid, value.ptgt_uid, value.pqos);
}
template <class T> bool same_request(const T &a, const T &b) {
  if constexpr (requires { a.prsvdc; })
    if (a.prsvdc != b.prsvdc)
      return false;
  if constexpr (requires { a.psec_usid1; })
    if (a.psec_usid1 != b.psec_usid1)
      return false;
  if constexpr (requires { a.pmecid_uor_ustream_uid; })
    if (a.pmecid_uor_ustream_uid != b.pmecid_uor_ustream_uid)
      return false;
  if constexpr (requires { a.ppbha; })
    if (a.ppbha != b.ppbha)
      return false;
  if constexpr (requires { a.pmpam; })
    if (a.pmpam != b.pmpam)
      return false;
  return request_fields(a) == request_fields(b);
}
template <class T, class Random>
void fill_request(T &value, Random random, unsigned node_width = 7,
                  unsigned address_width = 44) {
  value.ptrace_utag = random() & low_mask(1);
  value.ptag_uop = random() & low_mask(2);
  value.pexp_ucomp_uack = random() & low_mask(1);
  value.pexcl_usnoop_ume_ucah = random() & low_mask(1);
  value.pgroup_uid_uor_ulpid = random() & low_mask(8);
  value.psnp_uattr_uor_udo_udwt = random() & low_mask(1);
  value.pmem_uattr.pallocate = random() & low_mask(1);
  value.pmem_uattr.pcacheable = random() & low_mask(1);
  value.pmem_uattr.pdevice = random() & low_mask(1);
  value.pmem_uattr.pearly_uwrite_uacknowledge = random() & low_mask(1);
  value.ppcrd_utype = random() & low_mask(4);
  value.porder = random() & low_mask(2);
  value.pallow_uretry = random() & low_mask(1);
  value.plikely_ushared = random() & low_mask(1);
  value.ppas = random() & low_mask(3);
  value.paddress = random() & low_mask(address_width);
  value.psize_uor_unum_ureq = random() & low_mask(6);
  value.pmulti_ureq = random() & low_mask(1);
  value.popcode = random() & low_mask(7);
  value.preturn_utxn_uid_uor_ustash_ulpid = random() & low_mask(12);
  value.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint =
      random() & low_mask(1);
  value.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      random() & low_mask(node_width);
  value.ptxn_uid = random() & low_mask(12);
  value.psrc_uid = random() & low_mask(node_width);
  value.ptgt_uid = random() & low_mask(node_width);
  value.pqos = random() & low_mask(4);
  if constexpr (requires { value.prsvdc; })
    value.prsvdc = random() & low_mask(32);
  if constexpr (requires { value.psec_usid1; })
    value.psec_usid1 = random() & low_mask(1);
  if constexpr (requires { value.pmecid_uor_ustream_uid; })
    value.pmecid_uor_ustream_uid = random() & low_mask(16);
  if constexpr (requires { value.ppbha; })
    value.ppbha = random() & low_mask(4);
  if constexpr (requires { value.pmpam; })
    value.pmpam = random() & low_mask(15);
}
template <class T> auto response_fields(const T &value) {
  return std::tie(value.pcache_uline_uid, value.ptrace_utag, value.ptag_uop,
                  value.ppcrd_utype, value.pdbid_uor_ugroup_uid, value.pc_ubusy,
                  value.pfwd_ustate_uor_udata_upull, value.presp,
                  value.presp_uerr, value.popcode, value.ptxn_uid,
                  value.psrc_uid, value.ptgt_uid, value.pqos);
}
template <class T> bool same_response(const T &a, const T &b) {
  return response_fields(a) == response_fields(b);
}

template <class T> bool same_data(const T &a, const T &b) {
  if constexpr (requires { a.ppoison; })
    if (a.ppoison != b.ppoison)
      return false;
  if constexpr (requires { a.pdata_ucheck; })
    if (a.pdata_ucheck != b.pdata_ucheck)
      return false;
  if constexpr (requires { a.prsvdc; })
    if (a.prsvdc != b.prsvdc)
      return false;
  return std::tie(a.pdata, a.pbyte_uenable, a.preplicate, a.pnum_udat, a.pcah,
                  a.ptrace_utag, a.ptag_uupdate, a.ptag, a.ptag_uop,
                  a.pcache_uline_uid, a.pdata_uid, a.pccid, a.pdbid_uor_umecid,
                  a.pc_ubusy, a.pdata_upull, a.pdata_usource_uor_ufwd_ustate,
                  a.presp, a.presp_uerr, a.popcode,
                  a.phome_unid_uor_upbha_uor_umismatched_umecid, a.ptxn_uid,
                  a.psrc_uid, a.ptgt_uid, a.pqos) ==
         std::tie(b.pdata, b.pbyte_uenable, b.preplicate, b.pnum_udat, b.pcah,
                  b.ptrace_utag, b.ptag_uupdate, b.ptag, b.ptag_uop,
                  b.pcache_uline_uid, b.pdata_uid, b.pccid, b.pdbid_uor_umecid,
                  b.pc_ubusy, b.pdata_upull, b.pdata_usource_uor_ufwd_ustate,
                  b.presp, b.presp_uerr, b.popcode,
                  b.phome_unid_uor_upbha_uor_umismatched_umecid, b.ptxn_uid,
                  b.psrc_uid, b.ptgt_uid, b.pqos);
}

template <class T> bool same_snoop(const T &a, const T &b) {
  if constexpr (requires { a.pmecid; })
    if (a.pmecid != b.pmecid)
      return false;
  if constexpr (requires { a.pmpam; })
    if (a.pmpam != b.pmpam)
      return false;
  return std::tie(a.ptrace_utag, a.pret_uto_usrc, a.pdo_unot_ugo_uto_usd,
                  a.ppas, a.paddress, a.popcode,
                  a.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext,
                  a.pfwd_unid_uor_upbha, a.ptxn_uid, a.psrc_uid, a.pqos) ==
         std::tie(b.ptrace_utag, b.pret_uto_usrc, b.pdo_unot_ugo_uto_usd,
                  b.ppas, b.paddress, b.popcode,
                  b.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext,
                  b.pfwd_unid_uor_upbha, b.ptxn_uid, b.psrc_uid, b.pqos);
}

template <class T, class Random> void fill_response(T &value, Random random) {
  value.pcache_uline_uid = random() & low_mask(6);
  value.ptrace_utag = random() & low_mask(1);
  value.ptag_uop = random() & low_mask(2);
  value.ppcrd_utype = random() & low_mask(4);
  value.pdbid_uor_ugroup_uid = random() & low_mask(12);
  value.pc_ubusy = random() & low_mask(3);
  value.pfwd_ustate_uor_udata_upull = random() & low_mask(3);
  value.presp = random() & low_mask(3);
  value.presp_uerr = random() & low_mask(2);
  value.popcode = random() & low_mask(5);
  value.ptxn_uid = random() & low_mask(12);
  value.psrc_uid = random() & low_mask(7);
  value.ptgt_uid = random() & low_mask(7);
  value.pqos = random() & low_mask(4);
}

template <unsigned Width, class T, class Random>
void fill_data(T &value, Random random) {
  value.preplicate = random() & low_mask(1);
  value.pnum_udat = random() & low_mask(2);
  value.pcah = random() & low_mask(1);
  value.ptrace_utag = random() & low_mask(1);
  value.ptag_uop = random() & low_mask(2);
  value.pcache_uline_uid = random() & low_mask(6);
  value.pdata_uid = random() & low_mask(2);
  value.pccid = random() & low_mask(2);
  value.pdbid_uor_umecid = random() & low_mask(16);
  value.pc_ubusy = random() & low_mask(3);
  value.pdata_upull = random() & low_mask(1);
  value.pdata_usource_uor_ufwd_ustate = random() & low_mask(8);
  value.presp = random() & low_mask(3);
  value.presp_uerr = random() & low_mask(2);
  value.popcode = random() & low_mask(4);
  value.phome_unid_uor_upbha_uor_umismatched_umecid = random() & low_mask(7);
  value.ptxn_uid = random() & low_mask(12);
  value.psrc_uid = random() & low_mask(7);
  value.ptgt_uid = random() & low_mask(7);
  value.pqos = random() & low_mask(4);
  for (auto &word : value.pdata.words)
    word = random();
  value.pbyte_uenable =
      (std::uint64_t(random()) << 32 | random()) & low_mask(Width / 8);
  value.ptag_uupdate = random() & low_mask(Width / 128);
  value.ptag = random() & low_mask(Width / 32);
  if constexpr (requires { value.ppoison; })
    value.ppoison = random() & low_mask(Width / 64);
  if constexpr (requires { value.pdata_ucheck; })
    value.pdata_ucheck =
        (std::uint64_t(random()) << 32 | random()) & low_mask(Width / 8);
  if constexpr (requires { value.prsvdc; })
    value.prsvdc = random();
}

template <class T, class Random> void fill_snoop(T &value, Random random) {
  value.ptrace_utag = (std::uint64_t(random()) << 32 | random()) & low_mask(1);
  value.pret_uto_usrc =
      (std::uint64_t(random()) << 32 | random()) & low_mask(1);
  value.pdo_unot_ugo_uto_usd =
      (std::uint64_t(random()) << 32 | random()) & low_mask(1);
  value.ppas = (std::uint64_t(random()) << 32 | random()) & low_mask(3);
  value.paddress = (std::uint64_t(random()) << 32 | random()) & low_mask(41);
  value.popcode = (std::uint64_t(random()) << 32 | random()) & low_mask(5);
  value.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext =
      (std::uint64_t(random()) << 32 | random()) & low_mask(12);
  value.pfwd_unid_uor_upbha =
      (std::uint64_t(random()) << 32 | random()) & low_mask(7);
  value.ptxn_uid = (std::uint64_t(random()) << 32 | random()) & low_mask(12);
  value.psrc_uid = (std::uint64_t(random()) << 32 | random()) & low_mask(7);
  value.pqos = (std::uint64_t(random()) << 32 | random()) & low_mask(4);
}
