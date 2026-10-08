// Simulates transparent CHI activation, credited request, response, and credit
// wiring.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    ingress_in = {};
    egress_in = {};

    ingress_in.ptx_ulink_uactive_urequest = UINT64_C(1);
    ingress_in.prx_ulink_uactive_uack = UINT64_C(1);
    ingress_in.ptx_ureq.pvalid = UINT64_C(1);
    ingress_in.ptx_ureq.pbits.popcode = UINT64_C(4);
    ingress_in.ptx_ureq.pbits.paddress = UINT64_C(1250999896491);
    ingress_in.prx_ursp.pcredit = UINT64_C(1);
    egress_in.ptx_ureq.pcredit = UINT64_C(1);
    egress_in.ptx_ulink_uactive_uack = UINT64_C(1);
    egress_in.prx_ulink_uactive_urequest = UINT64_C(1);
    egress_in.prx_ursp.pvalid = UINT64_C(1);
    egress_in.prx_ursp.pbits.popcode = UINT64_C(4);
    egress_in.prx_ursp.pbits.ptxn_uid = UINT64_C(1445);
    eval();

    CHECK(egress_out.ptx_ulink_uactive_urequest ==
          ingress_in.ptx_ulink_uactive_urequest);
    CHECK(egress_out.prx_ulink_uactive_uack ==
          ingress_in.prx_ulink_uactive_uack);
    CHECK(egress_out.ptx_ureq.pvalid == ingress_in.ptx_ureq.pvalid);
    CHECK(egress_out.ptx_ureq.pbits.ptrace_utag ==
          ingress_in.ptx_ureq.pbits.ptrace_utag);
    CHECK(egress_out.ptx_ureq.pbits.ptag_uop ==
          ingress_in.ptx_ureq.pbits.ptag_uop);
    CHECK(egress_out.ptx_ureq.pbits.pexp_ucomp_uack ==
          ingress_in.ptx_ureq.pbits.pexp_ucomp_uack);
    CHECK(egress_out.ptx_ureq.pbits.pexcl_usnoop_ume_ucah ==
          ingress_in.ptx_ureq.pbits.pexcl_usnoop_ume_ucah);
    CHECK(egress_out.ptx_ureq.pbits.pgroup_uid_uor_ulpid ==
          ingress_in.ptx_ureq.pbits.pgroup_uid_uor_ulpid);
    CHECK(egress_out.ptx_ureq.pbits.psnp_uattr_uor_udo_udwt ==
          ingress_in.ptx_ureq.pbits.psnp_uattr_uor_udo_udwt);
    CHECK(egress_out.ptx_ureq.pbits.pmem_uattr.pallocate ==
          ingress_in.ptx_ureq.pbits.pmem_uattr.pallocate);
    CHECK(egress_out.ptx_ureq.pbits.pmem_uattr.pcacheable ==
          ingress_in.ptx_ureq.pbits.pmem_uattr.pcacheable);
    CHECK(egress_out.ptx_ureq.pbits.pmem_uattr.pdevice ==
          ingress_in.ptx_ureq.pbits.pmem_uattr.pdevice);
    CHECK(egress_out.ptx_ureq.pbits.pmem_uattr.pearly_uwrite_uacknowledge ==
          ingress_in.ptx_ureq.pbits.pmem_uattr.pearly_uwrite_uacknowledge);
    CHECK(egress_out.ptx_ureq.pbits.ppcrd_utype ==
          ingress_in.ptx_ureq.pbits.ppcrd_utype);
    CHECK(egress_out.ptx_ureq.pbits.porder == ingress_in.ptx_ureq.pbits.porder);
    CHECK(egress_out.ptx_ureq.pbits.pallow_uretry ==
          ingress_in.ptx_ureq.pbits.pallow_uretry);
    CHECK(egress_out.ptx_ureq.pbits.plikely_ushared ==
          ingress_in.ptx_ureq.pbits.plikely_ushared);
    CHECK(egress_out.ptx_ureq.pbits.ppas == ingress_in.ptx_ureq.pbits.ppas);
    CHECK(egress_out.ptx_ureq.pbits.paddress ==
          ingress_in.ptx_ureq.pbits.paddress);
    CHECK(egress_out.ptx_ureq.pbits.psize_uor_unum_ureq ==
          ingress_in.ptx_ureq.pbits.psize_uor_unum_ureq);
    CHECK(egress_out.ptx_ureq.pbits.pmulti_ureq ==
          ingress_in.ptx_ureq.pbits.pmulti_ureq);
    CHECK(egress_out.ptx_ureq.pbits.popcode ==
          ingress_in.ptx_ureq.pbits.popcode);
    CHECK(egress_out.ptx_ureq.pbits.preturn_utxn_uid_uor_ustash_ulpid ==
          ingress_in.ptx_ureq.pbits.preturn_utxn_uid_uor_ustash_ulpid);
    CHECK(egress_out.ptx_ureq.pbits
              .pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint ==
          ingress_in.ptx_ureq.pbits
              .pstash_unid_uvalid_uendian_udeep_uprefetch_utgt_uhint);
    CHECK(egress_out.ptx_ureq.pbits
              .preturn_unid_uor_ustash_unid_uor_udata_utarget ==
          ingress_in.ptx_ureq.pbits
              .preturn_unid_uor_ustash_unid_uor_udata_utarget);
    CHECK(egress_out.ptx_ureq.pbits.ptxn_uid ==
          ingress_in.ptx_ureq.pbits.ptxn_uid);
    CHECK(egress_out.ptx_ureq.pbits.psrc_uid ==
          ingress_in.ptx_ureq.pbits.psrc_uid);
    CHECK(egress_out.ptx_ureq.pbits.ptgt_uid ==
          ingress_in.ptx_ureq.pbits.ptgt_uid);
    CHECK(egress_out.ptx_ureq.pbits.pqos == ingress_in.ptx_ureq.pbits.pqos);
    CHECK(egress_out.ptx_ursp.pvalid == ingress_in.ptx_ursp.pvalid);
    CHECK(egress_out.ptx_ursp.pbits.pcache_uline_uid ==
          ingress_in.ptx_ursp.pbits.pcache_uline_uid);
    CHECK(egress_out.ptx_ursp.pbits.ptrace_utag ==
          ingress_in.ptx_ursp.pbits.ptrace_utag);
    CHECK(egress_out.ptx_ursp.pbits.ptag_uop ==
          ingress_in.ptx_ursp.pbits.ptag_uop);
    CHECK(egress_out.ptx_ursp.pbits.ppcrd_utype ==
          ingress_in.ptx_ursp.pbits.ppcrd_utype);
    CHECK(egress_out.ptx_ursp.pbits.pdbid_uor_ugroup_uid ==
          ingress_in.ptx_ursp.pbits.pdbid_uor_ugroup_uid);
    CHECK(egress_out.ptx_ursp.pbits.pc_ubusy ==
          ingress_in.ptx_ursp.pbits.pc_ubusy);
    CHECK(egress_out.ptx_ursp.pbits.pfwd_ustate_uor_udata_upull ==
          ingress_in.ptx_ursp.pbits.pfwd_ustate_uor_udata_upull);
    CHECK(egress_out.ptx_ursp.pbits.presp == ingress_in.ptx_ursp.pbits.presp);
    CHECK(egress_out.ptx_ursp.pbits.presp_uerr ==
          ingress_in.ptx_ursp.pbits.presp_uerr);
    CHECK(egress_out.ptx_ursp.pbits.popcode ==
          ingress_in.ptx_ursp.pbits.popcode);
    CHECK(egress_out.ptx_ursp.pbits.ptxn_uid ==
          ingress_in.ptx_ursp.pbits.ptxn_uid);
    CHECK(egress_out.ptx_ursp.pbits.psrc_uid ==
          ingress_in.ptx_ursp.pbits.psrc_uid);
    CHECK(egress_out.ptx_ursp.pbits.ptgt_uid ==
          ingress_in.ptx_ursp.pbits.ptgt_uid);
    CHECK(egress_out.ptx_ursp.pbits.pqos == ingress_in.ptx_ursp.pbits.pqos);
    CHECK(egress_out.ptx_udat.pvalid == ingress_in.ptx_udat.pvalid);
    CHECK(egress_out.ptx_udat.pbits.pbyte_uenable ==
          ingress_in.ptx_udat.pbits.pbyte_uenable);
    CHECK(egress_out.ptx_udat.pbits.preplicate ==
          ingress_in.ptx_udat.pbits.preplicate);
    CHECK(egress_out.ptx_udat.pbits.pnum_udat ==
          ingress_in.ptx_udat.pbits.pnum_udat);
    CHECK(egress_out.ptx_udat.pbits.pcah == ingress_in.ptx_udat.pbits.pcah);
    CHECK(egress_out.ptx_udat.pbits.ptrace_utag ==
          ingress_in.ptx_udat.pbits.ptrace_utag);
    CHECK(egress_out.ptx_udat.pbits.ptag_uupdate ==
          ingress_in.ptx_udat.pbits.ptag_uupdate);
    CHECK(egress_out.ptx_udat.pbits.ptag == ingress_in.ptx_udat.pbits.ptag);
    CHECK(egress_out.ptx_udat.pbits.ptag_uop ==
          ingress_in.ptx_udat.pbits.ptag_uop);
    CHECK(egress_out.ptx_udat.pbits.pcache_uline_uid ==
          ingress_in.ptx_udat.pbits.pcache_uline_uid);
    CHECK(egress_out.ptx_udat.pbits.pdata_uid ==
          ingress_in.ptx_udat.pbits.pdata_uid);
    CHECK(egress_out.ptx_udat.pbits.pccid == ingress_in.ptx_udat.pbits.pccid);
    CHECK(egress_out.ptx_udat.pbits.pdbid_uor_umecid ==
          ingress_in.ptx_udat.pbits.pdbid_uor_umecid);
    CHECK(egress_out.ptx_udat.pbits.pc_ubusy ==
          ingress_in.ptx_udat.pbits.pc_ubusy);
    CHECK(egress_out.ptx_udat.pbits.pdata_upull ==
          ingress_in.ptx_udat.pbits.pdata_upull);
    CHECK(egress_out.ptx_udat.pbits.pdata_usource_uor_ufwd_ustate ==
          ingress_in.ptx_udat.pbits.pdata_usource_uor_ufwd_ustate);
    CHECK(egress_out.ptx_udat.pbits.presp == ingress_in.ptx_udat.pbits.presp);
    CHECK(egress_out.ptx_udat.pbits.presp_uerr ==
          ingress_in.ptx_udat.pbits.presp_uerr);
    CHECK(egress_out.ptx_udat.pbits.popcode ==
          ingress_in.ptx_udat.pbits.popcode);
    CHECK(
        egress_out.ptx_udat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid ==
        ingress_in.ptx_udat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid);
    CHECK(egress_out.ptx_udat.pbits.ptxn_uid ==
          ingress_in.ptx_udat.pbits.ptxn_uid);
    CHECK(egress_out.ptx_udat.pbits.psrc_uid ==
          ingress_in.ptx_udat.pbits.psrc_uid);
    CHECK(egress_out.ptx_udat.pbits.ptgt_uid ==
          ingress_in.ptx_udat.pbits.ptgt_uid);
    CHECK(egress_out.ptx_udat.pbits.pqos == ingress_in.ptx_udat.pbits.pqos);
    CHECK(egress_out.prx_ursp.pcredit == ingress_in.prx_ursp.pcredit);
    CHECK(egress_out.prx_udat.pcredit == ingress_in.prx_udat.pcredit);
    CHECK(ingress_out.ptx_ureq.pcredit == egress_in.ptx_ureq.pcredit);
    CHECK(ingress_out.ptx_ursp.pcredit == egress_in.ptx_ursp.pcredit);
    CHECK(ingress_out.ptx_udat.pcredit == egress_in.ptx_udat.pcredit);
    CHECK(ingress_out.ptx_ulink_uactive_uack ==
          egress_in.ptx_ulink_uactive_uack);
    CHECK(ingress_out.prx_ulink_uactive_urequest ==
          egress_in.prx_ulink_uactive_urequest);
    CHECK(ingress_out.prx_ursp.pvalid == egress_in.prx_ursp.pvalid);
    CHECK(ingress_out.prx_ursp.pbits.pcache_uline_uid ==
          egress_in.prx_ursp.pbits.pcache_uline_uid);
    CHECK(ingress_out.prx_ursp.pbits.ptrace_utag ==
          egress_in.prx_ursp.pbits.ptrace_utag);
    CHECK(ingress_out.prx_ursp.pbits.ptag_uop ==
          egress_in.prx_ursp.pbits.ptag_uop);
    CHECK(ingress_out.prx_ursp.pbits.ppcrd_utype ==
          egress_in.prx_ursp.pbits.ppcrd_utype);
    CHECK(ingress_out.prx_ursp.pbits.pdbid_uor_ugroup_uid ==
          egress_in.prx_ursp.pbits.pdbid_uor_ugroup_uid);
    CHECK(ingress_out.prx_ursp.pbits.pc_ubusy ==
          egress_in.prx_ursp.pbits.pc_ubusy);
    CHECK(ingress_out.prx_ursp.pbits.pfwd_ustate_uor_udata_upull ==
          egress_in.prx_ursp.pbits.pfwd_ustate_uor_udata_upull);
    CHECK(ingress_out.prx_ursp.pbits.presp == egress_in.prx_ursp.pbits.presp);
    CHECK(ingress_out.prx_ursp.pbits.presp_uerr ==
          egress_in.prx_ursp.pbits.presp_uerr);
    CHECK(ingress_out.prx_ursp.pbits.popcode ==
          egress_in.prx_ursp.pbits.popcode);
    CHECK(ingress_out.prx_ursp.pbits.ptxn_uid ==
          egress_in.prx_ursp.pbits.ptxn_uid);
    CHECK(ingress_out.prx_ursp.pbits.psrc_uid ==
          egress_in.prx_ursp.pbits.psrc_uid);
    CHECK(ingress_out.prx_ursp.pbits.ptgt_uid ==
          egress_in.prx_ursp.pbits.ptgt_uid);
    CHECK(ingress_out.prx_ursp.pbits.pqos == egress_in.prx_ursp.pbits.pqos);
    CHECK(ingress_out.prx_udat.pvalid == egress_in.prx_udat.pvalid);
    CHECK(ingress_out.prx_udat.pbits.pbyte_uenable ==
          egress_in.prx_udat.pbits.pbyte_uenable);
    CHECK(ingress_out.prx_udat.pbits.preplicate ==
          egress_in.prx_udat.pbits.preplicate);
    CHECK(ingress_out.prx_udat.pbits.pnum_udat ==
          egress_in.prx_udat.pbits.pnum_udat);
    CHECK(ingress_out.prx_udat.pbits.pcah == egress_in.prx_udat.pbits.pcah);
    CHECK(ingress_out.prx_udat.pbits.ptrace_utag ==
          egress_in.prx_udat.pbits.ptrace_utag);
    CHECK(ingress_out.prx_udat.pbits.ptag_uupdate ==
          egress_in.prx_udat.pbits.ptag_uupdate);
    CHECK(ingress_out.prx_udat.pbits.ptag == egress_in.prx_udat.pbits.ptag);
    CHECK(ingress_out.prx_udat.pbits.ptag_uop ==
          egress_in.prx_udat.pbits.ptag_uop);
    CHECK(ingress_out.prx_udat.pbits.pcache_uline_uid ==
          egress_in.prx_udat.pbits.pcache_uline_uid);
    CHECK(ingress_out.prx_udat.pbits.pdata_uid ==
          egress_in.prx_udat.pbits.pdata_uid);
    CHECK(ingress_out.prx_udat.pbits.pccid == egress_in.prx_udat.pbits.pccid);
    CHECK(ingress_out.prx_udat.pbits.pdbid_uor_umecid ==
          egress_in.prx_udat.pbits.pdbid_uor_umecid);
    CHECK(ingress_out.prx_udat.pbits.pc_ubusy ==
          egress_in.prx_udat.pbits.pc_ubusy);
    CHECK(ingress_out.prx_udat.pbits.pdata_upull ==
          egress_in.prx_udat.pbits.pdata_upull);
    CHECK(ingress_out.prx_udat.pbits.pdata_usource_uor_ufwd_ustate ==
          egress_in.prx_udat.pbits.pdata_usource_uor_ufwd_ustate);
    CHECK(ingress_out.prx_udat.pbits.presp == egress_in.prx_udat.pbits.presp);
    CHECK(ingress_out.prx_udat.pbits.presp_uerr ==
          egress_in.prx_udat.pbits.presp_uerr);
    CHECK(ingress_out.prx_udat.pbits.popcode ==
          egress_in.prx_udat.pbits.popcode);
    CHECK(ingress_out.prx_udat.pbits
              .phome_unid_uor_upbha_uor_umismatched_umecid ==
          egress_in.prx_udat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid);
    CHECK(ingress_out.prx_udat.pbits.ptxn_uid ==
          egress_in.prx_udat.pbits.ptxn_uid);
    CHECK(ingress_out.prx_udat.pbits.psrc_uid ==
          egress_in.prx_udat.pbits.psrc_uid);
    CHECK(ingress_out.prx_udat.pbits.ptgt_uid ==
          egress_in.prx_udat.pbits.ptgt_uid);
    CHECK(ingress_out.prx_udat.pbits.pqos == egress_in.prx_udat.pbits.pqos);
  });
}
