// Checks CHI flits, service matching, and singleton/multiregion address maps
// including NodeID-zero hits.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    for (int addr = 0; addr < UINT64_C(0x3000); addr++) {
      bool hit;
      int target;
      hit = (addr >= UINT64_C(0x1000) && addr < UINT64_C(0x1100)) ||
            (addr >= UINT64_C(0x2000) && addr < UINT64_C(0x2040)) ||
            (addr >= UINT64_C(0x2080) && addr < UINT64_C(0x20c0));
      target = (addr >= UINT64_C(0x2000) && addr < UINT64_C(0x2040))   ? 3
               : (addr >= UINT64_C(0x2080) && addr < UINT64_C(0x20c0)) ? 6
                                                                       : 0;
      map_address = ((addr)&low_mask(44));
      eval();
      CHECK(home_lookup.pvalid == hit && home_lookup.pnode_uid == target &&
            subordinate_lookup.pvalid == hit &&
            subordinate_lookup.pnode_uid == target);
      CHECK(single_home_lookup.pvalid == (addr >= 0x1000 && addr < 0x1100) &&
            single_home_lookup.pnode_uid == 0 &&
            single_subordinate_lookup.pvalid == single_home_lookup.pvalid &&
            single_subordinate_lookup.pnode_uid ==
                single_home_lookup.pnode_uid);
    }

    for (int op = 0; op < 128; op++) {
      for (int sz = 0; sz < 8; sz++) {
        req_opcode = ((op)&low_mask(7));
        size = ((sz)&low_mask(3));
        eval();
        CHECK(match_byte == (op == 4 && sz == 0) &&
              match_line == (op == 4 && sz <= 6) &&
              match_middle == (op == 4 && sz >= 2 && sz <= 4) &&
              match_large == (op == 4 && sz >= 5 && sz <= 6));
      }
    }
    req.ptrace_utag = 1;
    req.ptag_uop = 1;
    req.pexp_ucomp_uack = 1;
    req.pexcl_usnoop_ume_ucah = 1;
    req.pgroup_uid_uor_ulpid = 1;
    req.psnp_uattr_uor_udo_udwt = 1;
    req.pmem_uattr.pallocate = 1;
    req.pmem_uattr.pcacheable = 1;
    req.pmem_uattr.pdevice = 1;
    req.pmem_uattr.pearly_uwrite_uacknowledge = 1;
    req.ppcrd_utype = 1;
    req.porder = 1;
    req.pallow_uretry = 1;
    req.plikely_ushared = 1;
    req.ppas = 1;
    req.paddress = 1;
    req.psize_uor_unum_ureq = 1;
    req.pmulti_ureq = 1;
    req.popcode = 1;
    req.preturn_utxn_uid_uor_ustash_ulpid = 1;
    req.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint = 1;
    req.preturn_unid_uor_ustash_unid_uor_udata_utarget = 1;
    req.ptxn_uid = 1;
    req.psrc_uid = 1;
    req.ptgt_uid = 1;
    req.pqos = 1;
    rsp.pcache_uline_uid = 1;
    rsp.ptrace_utag = 1;
    rsp.ptag_uop = 1;
    rsp.ppcrd_utype = 1;
    rsp.pdbid_uor_ugroup_uid = 1;
    rsp.pc_ubusy = 1;
    rsp.pfwd_ustate_uor_udata_upull = 1;
    rsp.presp = 1;
    rsp.presp_uerr = 1;
    rsp.popcode = 1;
    rsp.ptxn_uid = 1;
    rsp.psrc_uid = 1;
    rsp.ptgt_uid = 1;
    rsp.pqos = 1;
    snp.ptrace_utag = 1;
    snp.pret_uto_usrc = 1;
    snp.pdo_unot_ugo_uto_usd = 1;
    snp.ppas = 1;
    snp.paddress = 1;
    snp.popcode = 1;
    snp.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext = 1;
    snp.pfwd_unid_uor_upbha = 1;
    snp.ptxn_uid = 1;
    snp.psrc_uid = 1;
    snp.pqos = 1;
    dat.pdata.words.fill(0x89abcdef);
    dat.pbyte_uenable = 1;
    dat.preplicate = 1;
    dat.pnum_udat = 1;
    dat.pcah = 1;
    dat.ptrace_utag = 1;
    dat.ptag_uupdate = 1;
    dat.ptag = 1;
    dat.ptag_uop = 1;
    dat.pcache_uline_uid = 1;
    dat.pdata_uid = 1;
    dat.pccid = 1;
    dat.pdbid_uor_umecid = 1;
    dat.pc_ubusy = 1;
    dat.pdata_upull = 1;
    dat.pdata_usource_uor_ufwd_ustate = 1;
    dat.presp = 1;
    dat.presp_uerr = 1;
    dat.popcode = 1;
    dat.phome_unid_uor_upbha_uor_umismatched_umecid = 1;
    dat.ptxn_uid = 1;
    dat.psrc_uid = 1;
    dat.ptgt_uid = 1;
    dat.pqos = 1;
    req_opcode = UINT64_C(4);
    rsp_opcode = UINT64_C(5);
    snp_opcode = UINT64_C(23);
    dat_opcode = UINT64_C(3);
    size = UINT64_C(6);
    data_id = UINT64_C(3);
    eval();

    CHECK(dat_out.pdata == dat.pdata);
    CHECK(dat_out.pbyte_uenable == dat.pbyte_uenable);
    CHECK(dat_out.preplicate == dat.preplicate);
    CHECK(dat_out.pnum_udat == dat.pnum_udat);
    CHECK(dat_out.pcah == dat.pcah);
    CHECK(dat_out.ptrace_utag == dat.ptrace_utag);
    CHECK(dat_out.ptag_uupdate == dat.ptag_uupdate);
    CHECK(dat_out.ptag == dat.ptag);
    CHECK(dat_out.ptag_uop == dat.ptag_uop);
    CHECK(dat_out.pcache_uline_uid == dat.pcache_uline_uid);
    CHECK(dat_out.pdata_uid == dat.pdata_uid);
    CHECK(dat_out.pccid == dat.pccid);
    CHECK(dat_out.pdbid_uor_umecid == dat.pdbid_uor_umecid);
    CHECK(dat_out.pc_ubusy == dat.pc_ubusy);
    CHECK(dat_out.pdata_upull == dat.pdata_upull);
    CHECK(dat_out.pdata_usource_uor_ufwd_ustate ==
          dat.pdata_usource_uor_ufwd_ustate);
    CHECK(dat_out.presp == dat.presp);
    CHECK(dat_out.presp_uerr == dat.presp_uerr);
    CHECK(dat_out.popcode == dat.popcode);
    CHECK(dat_out.phome_unid_uor_upbha_uor_umismatched_umecid ==
          dat.phome_unid_uor_upbha_uor_umismatched_umecid);
    CHECK(dat_out.ptxn_uid == dat.ptxn_uid);
    CHECK(dat_out.psrc_uid == dat.psrc_uid);
    CHECK(dat_out.ptgt_uid == dat.ptgt_uid);
    CHECK(dat_out.pqos == dat.pqos);
    CHECK(req_out.ptrace_utag == req.ptrace_utag);
    CHECK(req_out.ptag_uop == req.ptag_uop);
    CHECK(req_out.pexp_ucomp_uack == req.pexp_ucomp_uack);
    CHECK(req_out.pexcl_usnoop_ume_ucah == req.pexcl_usnoop_ume_ucah);
    CHECK(req_out.pgroup_uid_uor_ulpid == req.pgroup_uid_uor_ulpid);
    CHECK(req_out.psnp_uattr_uor_udo_udwt == req.psnp_uattr_uor_udo_udwt);
    CHECK(req_out.pmem_uattr.pallocate == req.pmem_uattr.pallocate);
    CHECK(req_out.pmem_uattr.pcacheable == req.pmem_uattr.pcacheable);
    CHECK(req_out.pmem_uattr.pdevice == req.pmem_uattr.pdevice);
    CHECK(req_out.pmem_uattr.pearly_uwrite_uacknowledge ==
          req.pmem_uattr.pearly_uwrite_uacknowledge);
    CHECK(req_out.ppcrd_utype == req.ppcrd_utype);
    CHECK(req_out.porder == req.porder);
    CHECK(req_out.pallow_uretry == req.pallow_uretry);
    CHECK(req_out.plikely_ushared == req.plikely_ushared);
    CHECK(req_out.ppas == req.ppas);
    CHECK(req_out.paddress == req.paddress);
    CHECK(req_out.psize_uor_unum_ureq == req.psize_uor_unum_ureq);
    CHECK(req_out.pmulti_ureq == req.pmulti_ureq);
    CHECK(req_out.popcode == req.popcode);
    CHECK(req_out.preturn_utxn_uid_uor_ustash_ulpid ==
          req.preturn_utxn_uid_uor_ustash_ulpid);
    CHECK(req_out.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint ==
          req.pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint);
    CHECK(req_out.preturn_unid_uor_ustash_unid_uor_udata_utarget ==
          req.preturn_unid_uor_ustash_unid_uor_udata_utarget);
    CHECK(req_out.ptxn_uid == req.ptxn_uid);
    CHECK(req_out.psrc_uid == req.psrc_uid);
    CHECK(req_out.ptgt_uid == req.ptgt_uid);
    CHECK(req_out.pqos == req.pqos);
    CHECK(rsp_out.pcache_uline_uid == rsp.pcache_uline_uid);
    CHECK(rsp_out.ptrace_utag == rsp.ptrace_utag);
    CHECK(rsp_out.ptag_uop == rsp.ptag_uop);
    CHECK(rsp_out.ppcrd_utype == rsp.ppcrd_utype);
    CHECK(rsp_out.pdbid_uor_ugroup_uid == rsp.pdbid_uor_ugroup_uid);
    CHECK(rsp_out.pc_ubusy == rsp.pc_ubusy);
    CHECK(rsp_out.pfwd_ustate_uor_udata_upull ==
          rsp.pfwd_ustate_uor_udata_upull);
    CHECK(rsp_out.presp == rsp.presp);
    CHECK(rsp_out.presp_uerr == rsp.presp_uerr);
    CHECK(rsp_out.popcode == rsp.popcode);
    CHECK(rsp_out.ptxn_uid == rsp.ptxn_uid);
    CHECK(rsp_out.psrc_uid == rsp.psrc_uid);
    CHECK(rsp_out.ptgt_uid == rsp.ptgt_uid);
    CHECK(rsp_out.pqos == rsp.pqos);
    CHECK(snp_out.ptrace_utag == snp.ptrace_utag);
    CHECK(snp_out.pret_uto_usrc == snp.pret_uto_usrc);
    CHECK(snp_out.pdo_unot_ugo_uto_usd == snp.pdo_unot_ugo_uto_usd);
    CHECK(snp_out.ppas == snp.ppas);
    CHECK(snp_out.paddress == snp.paddress);
    CHECK(snp_out.popcode == snp.popcode);
    CHECK(snp_out.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext ==
          snp.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext);
    CHECK(snp_out.pfwd_unid_uor_upbha == snp.pfwd_unid_uor_upbha);
    CHECK(snp_out.ptxn_uid == snp.ptxn_uid);
    CHECK(snp_out.psrc_uid == snp.psrc_uid);
    CHECK(snp_out.pqos == snp.pqos);
    CHECK(req_opcode_valid && rsp_opcode_valid && snp_opcode_valid &&
          dat_opcode_valid);
    CHECK(req_read_no_snp && !req_write_no_snp && !req_atomic);
    CHECK(rsp_allocates_dbid && dat_write_data && !dat_response_data);
    CHECK(size_valid && data_beats_minus_one == UINT64_C(3) && data_id_valid);
    CHECK(req_size == slice(req_num_req, 2, 0));

    req_opcode = UINT64_C(6);
    rsp_opcode = UINT64_C(15);
    snp_opcode = UINT64_C(14);
    dat_opcode = UINT64_C(8);
    size = UINT64_C(7);
    eval();
    CHECK(!req_opcode_valid && !rsp_opcode_valid && !snp_opcode_valid &&
          !dat_opcode_valid);
    CHECK(!size_valid);

    req_opcode = UINT64_C(40);
    rsp_opcode = UINT64_C(4);
    dat_opcode = UINT64_C(4);
    size = UINT64_C(5);
    eval();
    CHECK(req_atomic && !rsp_allocates_dbid);
    CHECK(!dat_write_data && dat_response_data &&
          data_beats_minus_one == UINT64_C(1));
  });
}
