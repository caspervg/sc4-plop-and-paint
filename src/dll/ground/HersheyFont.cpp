#include "HersheyFont.hpp"

namespace Ground::Hershey {
    namespace {
        // ASCII 32 to 126, from futural.jhf (https://github.com/kamalmostafa/hershey-fonts).
        constexpr const char* kGlyphs[] = {
            "JZ",  // space
            "MWRFRT RRYQZR[SZRY",  // !
            "JZNFNM RVFVM",  // "
            "H]SBLb RYBRb RLOZO RKUYU",  // #
            "H\\PBP_ RTBT_ RYIWGTFPFMGKIKKLMMNOOUQWRXSYUYXWZT[P[MZKX",  // $
            "F^[FI[ RNFPHPJOLMMKMIKIIJGLFNFPGSHVHYG[F RWTUUTWTYV[X[ZZ[X[VYTWT",  // %
            "E_\\O\\N[MZMYNXPVUTXRZP[L[JZIYHWHUISJRQNRMSKSIRGPFNGMIMKNNPQUXWZY[[[\\Z\\Y",  // &
            "MWRHQGRFSGSIRKQL",  // '
            "KYVBTDRGPKOPOTPYR]T`Vb",  // (
            "KYNBPDRGTKUPUTTYR]P`Nb",  // )
            "JZRLRX RMOWU RWOMU",  // asterisk
            "E_RIR[ RIR[R",  // +
            "NVSWRXQWRVSWSYQ[",  // ,
            "E_IR[R",  // -
            "NVRVQWRXSWRV",  // .
            "G][BIb",  // /
            "H\\QFNGLJKOKRLWNZQ[S[VZXWYRYOXJVGSFQF",  // 0
            "H\\NJPISFS[",  // 1
            "H\\LKLJMHNGPFTFVGWHXJXLWNUQK[Y[",  // 2
            "H\\MFXFRNUNWOXPYSYUXXVZS[P[MZLYKW",  // 3
            "H\\UFKTZT RUFU[",  // 4
            "H\\WFMFLOMNPMSMVNXPYSYUXXVZS[P[MZLYKW",  // 5
            "H\\XIWGTFRFOGMJLOLTMXOZR[S[VZXXYUYTXQVOSNRNOOMQLT",  // 6
            "H\\YFO[ RKFYF",  // 7
            "H\\PFMGLILKMMONSOVPXRYTYWXYWZT[P[MZLYKWKTLRNPQOUNWMXKXIWGTFPF",  // 8
            "H\\XMWPURRSQSNRLPKMKLLINGQFRFUGWIXMXRWWUZR[P[MZLX",  // 9
            "NVROQPRQSPRO RRVQWRXSWRV",  // :
            "NVROQPRQSPRO RSWRXQWRVSWSYQ[",  // ;
            "F^ZIJRZ[",  // <
            "E_IO[O RIU[U",  // =
            "F^JIZRJ[",  // >
            "I[LKLJMHNGPFTFVGWHXJXLWNVORQRT RRYQZR[SZRY",  // ?
            "E`WNVLTKQKOLNMMPMSNUPVSVUUVS RQKOMNPNSOUPV RWKVSVUXVZV\\T]Q]O\\L[JYHWGTFQFNGLHJJILHOHRIUJWLYNZQ[T[WZYYZX RXKWSWUXV",  // @
            "I[RFJ[ RRFZ[ RMTWT",  // A
            "G\\KFK[ RKFTFWGXHYJYLXNWOTP RKPTPWQXRYTYWXYWZT[K[",  // B
            "H]ZKYIWGUFQFOGMILKKNKSLVMXOZQ[U[WZYXZV",  // C
            "G\\KFK[ RKFRFUGWIXKYNYSXVWXUZR[K[",  // D
            "H[LFL[ RLFYF RLPTP RL[Y[",  // E
            "HZLFL[ RLFYF RLPTP",  // F
            "H]ZKYIWGUFQFOGMILKKNKSLVMXOZQ[U[WZYXZVZS RUSZS",  // G
            "G]KFK[ RYFY[ RKPYP",  // H
            "NVRFR[",  // I
            "JZVFVVUYTZR[P[NZMYLVLT",  // J
            "G\\KFK[ RYFKT RPOY[",  // K
            "HYLFL[ RL[X[",  // L
            "F^JFJ[ RJFR[ RZFR[ RZFZ[",  // M
            "G]KFK[ RKFY[ RYFY[",  // N
            "G]PFNGLIKKJNJSKVLXNZP[T[VZXXYVZSZNYKXIVGTFPF",  // O
            "G\\KFK[ RKFTFWGXHYJYMXOWPTQKQ",  // P
            "G]PFNGLIKKJNJSKVLXNZP[T[VZXXYVZSZNYKXIVGTFPF RSWY]",  // Q
            "G\\KFK[ RKFTFWGXHYJYLXNWOTPKP RRPY[",  // R
            "H\\YIWGTFPFMGKIKKLMMNOOUQWRXSYUYXWZT[P[MZKX",  // S
            "JZRFR[ RKFYF",  // T
            "G]KFKULXNZQ[S[VZXXYUYF",  // U
            "I[JFR[ RZFR[",  // V
            "F^HFM[ RRFM[ RRFW[ R\\FW[",  // W
            "H\\KFY[ RYFK[",  // X
            "I[JFRPR[ RZFRP",  // Y
            "H\\YFK[ RKFYF RK[Y[",  // Z
            "KYOBOb RPBPb ROBVB RObVb",  // [
            "KYKFY^",  // backslash
            "KYTBTb RUBUb RNBUB RNbUb",  // ]
            "JZRDJR RRDZR",  // ^
            "I[Ib[b",  // _
            "NVSKQMQORPSORNQO",  // `
            "I\\XMX[ RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",  // a
            "H[LFL[ RLPNNPMSMUNWPXSXUWXUZS[P[NZLX",  // b
            "I[XPVNTMQMONMPLSLUMXOZQ[T[VZXX",  // c
            "I\\XFX[ RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",  // d
            "I[LSXSXQWOVNTMQMONMPLSLUMXOZQ[T[VZXX",  // e
            "MYWFUFSGRJR[ ROMVM",  // f
            "I\\XMX]W`VaTbQbOa RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",  // g
            "I\\MFM[ RMQPNRMUMWNXQX[",  // h
            "NVQFRGSFREQF RRMR[",  // i
            "MWRFSGTFSERF RSMS^RaPbNb",  // j
            "IZMFM[ RWMMW RQSX[",  // k
            "NVRFR[",  // l
            "CaGMG[ RGQJNLMOMQNRQR[ RRQUNWMZM\\N]Q][",  // m
            "I\\MMM[ RMQPNRMUMWNXQX[",  // n
            "I\\QMONMPLSLUMXOZQ[T[VZXXYUYSXPVNTMQM",  // o
            "H[LMLb RLPNNPMSMUNWPXSXUWXUZS[P[NZLX",  // p
            "I\\XMXb RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",  // q
            "KXOMO[ ROSPPRNTMWM",  // r
            "J[XPWNTMQMNNMPNRPSUTWUXWXXWZT[Q[NZMX",  // s
            "MYRFRWSZU[W[ ROMVM",  // t
            "I\\MMMWNZP[S[UZXW RXMX[",  // u
            "JZLMR[ RXMR[",  // v
            "G]JMN[ RRMN[ RRMV[ RZMV[",  // w
            "J[MMX[ RXMM[",  // x
            "JZLMR[ RXMR[P_NaLbKb",  // y
            "J[XMM[ RMMXM RM[X[",  // z
            "KYTBRCQDPFPHQJRKSMSOQQ RRCQEQGRISJTLTNSPORSTTVTXSZR[Q]Q_Ra RQSSUSWRYQZP\\P^Q`RaTb",  // {
            "NVRBRb",  // |
            "KYPBRCSDTFTHSJRKQMQOSQ RRCSESGRIQJPLPNQPURQTPVPXQZR[S]S_Ra RSSQUQWRYSZT\\T^S`RaPb",  // }
            "F^IUISJPLONOPPTSVTXTZS[Q RISJQLPNPPQTTVUXUZT[Q[O",  // ~
        };
        static_assert(sizeof(kGlyphs) / sizeof(kGlyphs[0]) == 95);
    }

    const char* Glyph(const char c) {
        const auto code = static_cast<unsigned char>(c);
        return code >= 32 && code <= 126 ? kGlyphs[code - 32] : nullptr;
    }
}
