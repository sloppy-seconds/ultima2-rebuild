# sub_8387 — mechanical clean (bulk wave); behavior not yet reviewed
# func 8387..8CCF  len 0949 B  evidence: reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# PROGRAM ENTRY POINT LIVES IN THIS RANGE, at L_008ac0. The MZ header's initial CS:IP is
#           0x8ac:0x0000, which is image-relative, so the first instruction DOS transfers to is
#           image 0x8ac0 = file 0x8cc0. The bytes there are the textbook MZ startup prologue:
#             8c d8   mov ax,es
#             8e c0   mov es,ax
#             05 10 00 add ax,0x0010
#             8e d0   mov ss,ax
#             bc 00 00 mov sp,0x0000
#           Recorded here rather than as a rename because this file's start (0x8387) is a
#           discovered routine start and 0x8ac0 is not that routine's beginning; splitting the
#           tree at 0x8ac0 is a structural change to the CP0 tiling and needs its own decision.
#           Until then this header is the only place in the repo that names the program entry,
#           which is worth knowing given the whole tree was seeded from the runtime CFG and the
#           CFG records what ran, not what was configured to run first.
sub_8387:
L_008587:
        db 0xff,0x04  # split: branch target inside data
L_008589:
        db 0x10,0xff  # split: branch target inside data
L_00858b:
        db 0xff  # split: branch target inside data
L_00858c:
        db 0xff  # split: branch target inside data
L_00858d:
        db 0xff,0x00  # split: branch target inside data
L_00858f:
        db 0x00,0x00  # split: branch target inside data
L_008591:
        db 0x00,0x00  # split: branch target inside data
L_008593:
        db 0x00,0xff  # split: branch target inside data
L_008595:
        db 0xc0,0x00,0x03  # split: branch target inside data
L_008598:
        db 0xcf  # split: branch target inside data
L_008599:
        db 0x00,0x00  # split: branch target inside data
L_00859b:
        db 0x0f,0x03,0x00  # split: branch target inside data
L_00859e:
        db 0x00,0x3f  # split: branch target inside data
L_0085a0:
        db 0x00,0x00  # split: branch target inside data
L_0085a2:
        db 0x00,0x3c  # split: branch target inside data
L_0085a4:
        db 0x0c,0x00  # split: branch target inside data
L_0085a6:
        db 0x00,0xff  # split: branch target inside data
L_0085a8:
        db 0xfc  # split: branch target inside data
L_0085a9:
        db 0x00,0x00  # split: branch target inside data
L_0085ab:
        db 0xfc  # split: branch target inside data
L_0085ac:
        db 0x3c,0x00  # split: branch target inside data
L_0085ae:
        db 0x00,0x3c  # split: branch target inside data
L_0085b0:
        db 0x0c,0x00  # split: branch target inside data
L_0085b2:
        db 0x00,0x3f  # split: branch target inside data
L_0085b4:
        db 0x00,0x00  # split: branch target inside data
L_0085b6:
        db 0x00,0x0f  # split: branch target inside data
L_0085b8:
        db 0x03,0x00  # split: branch target inside data
L_0085ba:
        db 0x00,0x03  # split: branch target inside data
L_0085bc:
        db 0xcf  # split: branch target inside data
L_0085bd:
        db 0x00,0x00  # split: branch target inside data
L_0085bf:
        db 0x00,0xfc  # split: branch target inside data
L_0085c1:
        db 0x00,0x00  # split: branch target inside data
L_0085c3:
        db 0x00,0x00  # split: branch target inside data
L_0085c5:
        db 0x00,0xff  # split: branch target inside data
L_0085c7:
        db 0xff  # split: branch target inside data
L_0085c8:
        db 0xff  # split: branch target inside data
L_0085c9:
        db 0xff,0x04  # split: branch target inside data
L_0085cb:
        db 0x10,0xff  # split: branch target inside data
L_0085cd:
        db 0xff  # split: branch target inside data
L_0085ce:
        db 0xff  # split: branch target inside data
L_0085cf:
        db 0xff,0x00  # split: branch target inside data
L_0085d1:
        db 0x00,0x00  # split: branch target inside data
L_0085d3:
        db 0x00,0x00  # split: branch target inside data
L_0085d5:
        db 0xff  # split: branch target inside data
L_0085d6:
        db 0xff,0xc0  # split: branch target inside data
L_0085d8:
        db 0x00,0x3c  # split: branch target inside data
L_0085da:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0085dd:
        db 0x3c,0x03  # split: branch target inside data
L_0085df:
        db 0x00,0x00  # split: branch target inside data
L_0085e1:
        db 0x3c,0x00  # split: branch target inside data
L_0085e3:
        db 0x00,0x00  # split: branch target inside data
L_0085e5:
        db 0x3c,0x00  # split: branch target inside data
L_0085e7:
        db 0x00,0x00  # split: branch target inside data
L_0085e9:
        db 0x3c,0x30  # split: branch target inside data
L_0085eb:
        db 0x00,0x00  # split: branch target inside data
L_0085ed:
        db 0x3f  # split: branch target inside data
L_0085ee:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_0085f1:
        db 0x3c,0xf0  # split: branch target inside data
L_0085f3:
        db 0x00,0x00  # split: branch target inside data
L_0085f5:
        db 0x3c,0x30  # split: branch target inside data
L_0085f7:
        db 0x00,0x00  # split: branch target inside data
L_0085f9:
        db 0x3c,0x00  # split: branch target inside data
L_0085fb:
        db 0x00,0x00  # split: branch target inside data
L_0085fd:
        db 0x3c,0x00  # split: branch target inside data
L_0085ff:
        db 0x00,0x00  # split: branch target inside data
L_008601:
        db 0xff,0xc0  # split: branch target inside data
L_008603:
        db 0x00,0x00  # split: branch target inside data
L_008605:
        db 0x00,0x00  # split: branch target inside data
L_008607:
        db 0x00,0xff  # split: branch target inside data
L_008609:
        db 0xff  # split: branch target inside data
L_00860a:
        db 0xff  # split: branch target inside data
L_00860b:
        db 0xff,0x04  # split: branch target inside data
L_00860d:
        db 0x10,0xff  # split: branch target inside data
L_00860f:
        db 0xff  # split: branch target inside data
L_008610:
        db 0xff  # split: branch target inside data
L_008611:
        db 0xff,0x00  # split: branch target inside data
L_008613:
        db 0x00,0x00  # split: branch target inside data
L_008615:
        db 0x00,0x00  # split: branch target inside data
L_008617:
        db 0x03,0xff  # split: branch target inside data
L_008619:
        db 0x00,0x00  # split: branch target inside data
L_00861b:
        db 0x0f,0x3c  # split: branch target inside data
L_00861d:
        db 0x00,0x00  # split: branch target inside data
L_00861f:
        db 0x3c,0x3c  # split: branch target inside data
L_008621:
        db 0x00,0x00  # split: branch target inside data
L_008623:
        db 0xfc  # split: branch target inside data
L_008624:
        db 0x0c,0x00  # split: branch target inside data
L_008626:
        db 0x00,0xf0  # split: branch target inside data
L_008628:
        db 0x00,0x00  # split: branch target inside data
L_00862a:
        db 0x03,0xf0  # split: branch target inside data
L_00862c:
        db 0x00,0x00  # split: branch target inside data
L_00862e:
        db 0x03,0xf0  # split: branch target inside data
L_008630:
        db 0xfc  # split: branch target inside data
L_008631:
        db 0x00,0x00  # split: branch target inside data
L_008633:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008637:
        db 0xfc  # split: branch target inside data
L_008638:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_00863b:
        db 0x3c,0x0f  # split: branch target inside data
L_00863d:
        db 0x00,0x00  # split: branch target inside data
L_00863f:
        db 0x0f,0x3c  # split: branch target inside data
L_008641:
        db 0x00,0x00  # split: branch target inside data
L_008643:
        db 0x03,0xf0  # split: branch target inside data
L_008645:
        db 0x00,0x00  # split: branch target inside data
L_008647:
        db 0x00,0x00  # split: branch target inside data
L_008649:
        db 0x00,0xff  # split: branch target inside data
L_00864b:
        db 0xff  # split: branch target inside data
L_00864c:
        db 0xff  # split: branch target inside data
L_00864d:
        db 0xff,0x04  # split: branch target inside data
L_00864f:
        db 0x10,0xff  # split: branch target inside data
L_008651:
        db 0xff  # split: branch target inside data
L_008652:
        db 0xff  # split: branch target inside data
L_008653:
        db 0xff,0x00  # split: branch target inside data
L_008655:
        db 0x00,0x00  # split: branch target inside data
L_008657:
        db 0x00,0x03  # split: branch target inside data
L_008659:
        db 0xff  # split: branch target inside data
L_00865a:
        db 0xff,0x00  # split: branch target inside data
L_00865c:
        db 0x00,0xf0  # split: branch target inside data
L_00865e:
        db 0x03,0x00  # split: branch target inside data
L_008660:
        db 0x00,0xf0  # split: branch target inside data
L_008662:
        db 0x00,0x00  # split: branch target inside data
L_008664:
        db 0x00,0xf3  # split: branch target inside data
L_008666:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008669:
        db 0xff,0xf0  # split: branch target inside data
L_00866b:
        db 0x00,0x00  # split: branch target inside data
L_00866d:
        db 0xf0,0xfc  # split: branch target inside data
L_00866f:
        db 0x00,0x00  # split: branch target inside data
L_008671:
        db 0xf0,0x3f  # split: branch target inside data
L_008673:
        db 0x00,0x00  # split: branch target inside data
L_008675:
        db 0xf0,0x3f  # split: branch target inside data
L_008677:
        db 0x00,0x00  # split: branch target inside data
L_008679:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_00867c:
        db 0x00,0xf0  # split: branch target inside data
L_00867e:
        db 0x3c,0x00  # split: branch target inside data
L_008680:
        db 0x00,0xf0  # split: branch target inside data
L_008682:
        db 0x30,0x00  # split: branch target inside data
L_008684:
        db 0x03,0xfc  # split: branch target inside data
L_008686:
        db 0x3f  # split: branch target inside data
L_008687:
        db 0x00,0x00  # split: branch target inside data
L_008689:
        db 0x00,0x00  # split: branch target inside data
L_00868b:
        db 0x00,0xff  # split: branch target inside data
L_00868d:
        db 0xff  # split: branch target inside data
L_00868e:
        db 0xff  # split: branch target inside data
L_00868f:
        db 0xff,0x04  # split: branch target inside data
L_008691:
        db 0x10,0xff  # split: branch target inside data
L_008693:
        db 0xff  # split: branch target inside data
L_008694:
        db 0xff  # split: branch target inside data
L_008695:
        db 0xff,0x00  # split: branch target inside data
L_008697:
        db 0x00,0x00  # split: branch target inside data
L_008699:
        db 0x00,0x00  # split: branch target inside data
L_00869b:
        db 0x3f  # split: branch target inside data
L_00869c:
        db 0xfc  # split: branch target inside data
L_00869d:
        db 0x00,0x00  # split: branch target inside data
L_00869f:
        db 0x03,0xc0  # split: branch target inside data
L_0086a1:
        db 0x00,0x00  # split: branch target inside data
L_0086a3:
        db 0x03,0xc0  # split: branch target inside data
L_0086a5:
        db 0x00,0x00  # split: branch target inside data
L_0086a7:
        db 0x03,0xc0  # split: branch target inside data
L_0086a9:
        db 0x00,0x00  # split: branch target inside data
L_0086ab:
        db 0x03,0xc0  # split: branch target inside data
L_0086ad:
        db 0x00,0x00  # split: branch target inside data
L_0086af:
        db 0x03,0xc0  # split: branch target inside data
L_0086b1:
        db 0x00,0x00  # split: branch target inside data
L_0086b3:
        db 0x03,0xc0  # split: branch target inside data
L_0086b5:
        db 0x00,0x00  # split: branch target inside data
L_0086b7:
        db 0x03,0xc0  # split: branch target inside data
L_0086b9:
        db 0x00,0x00  # split: branch target inside data
L_0086bb:
        db 0x03,0xc0  # split: branch target inside data
L_0086bd:
        db 0x00,0x00  # split: branch target inside data
L_0086bf:
        db 0x03,0xc0  # split: branch target inside data
L_0086c1:
        db 0x00,0x00  # split: branch target inside data
L_0086c3:
        db 0x03,0xc0  # split: branch target inside data
L_0086c5:
        db 0x00,0x00  # split: branch target inside data
L_0086c7:
        db 0x3f  # split: branch target inside data
L_0086c8:
        db 0xfc  # split: branch target inside data
L_0086c9:
        db 0x00,0x00  # split: branch target inside data
L_0086cb:
        db 0x00,0x00  # split: branch target inside data
L_0086cd:
        db 0x00,0xff  # split: branch target inside data
L_0086cf:
        db 0xff  # split: branch target inside data
L_0086d0:
        db 0xff  # split: branch target inside data
L_0086d1:
        db 0xff,0x04  # split: branch target inside data
L_0086d3:
        db 0x10,0xff  # split: branch target inside data
L_0086d5:
        db 0xff  # split: branch target inside data
L_0086d6:
        db 0xff  # split: branch target inside data
L_0086d7:
        db 0xff,0x00  # split: branch target inside data
L_0086d9:
        db 0x00,0x00  # split: branch target inside data
L_0086db:
        db 0x00,0x00  # split: branch target inside data
L_0086dd:
        db 0x0f,0xff,0xc0  # split: branch target inside data
L_0086e0:
        db 0x00,0x00  # split: branch target inside data
L_0086e2:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0086e5:
        db 0x00,0x0f  # split: branch target inside data
L_0086e7:
        db 0x00,0x00  # split: branch target inside data
L_0086e9:
        db 0x00,0x0f  # split: branch target inside data
L_0086eb:
        db 0x00,0x00  # split: branch target inside data
L_0086ed:
        db 0x00,0x0f  # split: branch target inside data
L_0086ef:
        db 0x00,0x00  # split: branch target inside data
L_0086f1:
        db 0x00,0x0f  # split: branch target inside data
L_0086f3:
        db 0x00,0x00  # split: branch target inside data
L_0086f5:
        db 0xc0,0x0f,0x00  # split: branch target inside data
L_0086f8:
        db 0x00,0xc0  # split: branch target inside data
L_0086fa:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0086fd:
        db 0xc0,0x0f,0x00  # split: branch target inside data
L_008700:
        db 0x00,0xf0  # split: branch target inside data
L_008702:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008705:
        db 0xfc  # split: branch target inside data
L_008706:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008709:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_00870c:
        db 0x00,0x00  # split: branch target inside data
L_00870e:
        db 0x00,0x00  # split: branch target inside data
L_008710:
        db 0xff  # split: branch target inside data
L_008711:
        db 0xff  # split: branch target inside data
L_008712:
        db 0xff  # split: branch target inside data
L_008713:
        db 0xff,0x04  # split: branch target inside data
L_008715:
        db 0x10,0xff  # split: branch target inside data
L_008717:
        db 0xff  # split: branch target inside data
L_008718:
        db 0xff  # split: branch target inside data
L_008719:
        db 0xff,0x00  # split: branch target inside data
L_00871b:
        db 0x00,0x00  # split: branch target inside data
L_00871d:
        db 0x00,0x00  # split: branch target inside data
L_00871f:
        db 0xff  # split: branch target inside data
L_008720:
        db 0xff,0x00  # split: branch target inside data
L_008722:
        db 0x00,0x3c  # split: branch target inside data
L_008724:
        db 0x03,0x00  # split: branch target inside data
L_008726:
        db 0x00,0x3c  # split: branch target inside data
L_008728:
        db 0x30,0x00  # split: branch target inside data
L_00872a:
        db 0x00,0x3c  # split: branch target inside data
L_00872c:
        db 0xfc  # split: branch target inside data
L_00872d:
        db 0x00,0x00  # split: branch target inside data
L_00872f:
        db 0x3f  # split: branch target inside data
L_008730:
        db 0xcf  # split: branch target inside data
L_008731:
        db 0x00,0x00  # split: branch target inside data
L_008733:
        db 0x3c,0x0f  # split: branch target inside data
L_008735:
        db 0x00,0x00  # split: branch target inside data
L_008737:
        db 0x3c,0x3c  # split: branch target inside data
L_008739:
        db 0x00,0x00  # split: branch target inside data
L_00873b:
        db 0x3f  # split: branch target inside data
L_00873c:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_00873f:
        db 0x3c,0xfc  # split: branch target inside data
L_008741:
        db 0x00,0x00  # split: branch target inside data
L_008743:
        db 0x3c,0x3f  # split: branch target inside data
L_008745:
        db 0x00,0x00  # split: branch target inside data
L_008747:
        db 0x3c,0x0f  # split: branch target inside data
L_008749:
        db 0x00,0x00  # split: branch target inside data
L_00874b:
        db 0xff,0x0f  # split: branch target inside data
L_00874d:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008750:
        db 0x00,0x00  # split: branch target inside data
L_008752:
        db 0xff  # split: branch target inside data
L_008753:
        db 0xff  # split: branch target inside data
L_008754:
        db 0xff  # split: branch target inside data
L_008755:
        db 0xff,0x04  # split: branch target inside data
L_008757:
        db 0x10,0xff  # split: branch target inside data
L_008759:
        db 0xff  # split: branch target inside data
L_00875a:
        db 0xff  # split: branch target inside data
L_00875b:
        db 0xff,0x00  # split: branch target inside data
L_00875d:
        db 0x00,0x00  # split: branch target inside data
L_00875f:
        db 0x00,0x00  # split: branch target inside data
L_008761:
        db 0xff  # split: branch target inside data
L_008762:
        db 0xfc  # split: branch target inside data
L_008763:
        db 0x00,0x00  # split: branch target inside data
L_008765:
        db 0x3c,0x00  # split: branch target inside data
L_008767:
        db 0x00,0x00  # split: branch target inside data
L_008769:
        db 0x3c,0x00  # split: branch target inside data
L_00876b:
        db 0x00,0x00  # split: branch target inside data
L_00876d:
        db 0x3c,0x00  # split: branch target inside data
L_00876f:
        db 0x00,0x00  # split: branch target inside data
L_008771:
        db 0x3c,0x00  # split: branch target inside data
L_008773:
        db 0x00,0x00  # split: branch target inside data
L_008775:
        db 0x3c,0x03  # split: branch target inside data
L_008777:
        db 0x00,0x00  # split: branch target inside data
L_008779:
        db 0x3c,0x03  # split: branch target inside data
L_00877b:
        db 0x00,0x00  # split: branch target inside data
L_00877d:
        db 0x3c,0x03  # split: branch target inside data
L_00877f:
        db 0x00,0x00  # split: branch target inside data
L_008781:
        db 0x3c,0x03  # split: branch target inside data
L_008783:
        db 0x00,0x00  # split: branch target inside data
L_008785:
        db 0x3c,0x0f  # split: branch target inside data
L_008787:
        db 0x00,0x00  # split: branch target inside data
L_008789:
        db 0x3c,0x3f  # split: branch target inside data
L_00878b:
        db 0x00,0x00  # split: branch target inside data
L_00878d:
        db 0xff  # split: branch target inside data
L_00878e:
        db 0xff,0x00  # split: branch target inside data
L_008790:
        db 0x00,0x00  # split: branch target inside data
L_008792:
        db 0x00,0x00  # split: branch target inside data
L_008794:
        db 0xff  # split: branch target inside data
L_008795:
        db 0xff  # split: branch target inside data
L_008796:
        db 0xff  # split: branch target inside data
L_008797:
        db 0xff,0x04  # split: branch target inside data
L_008799:
        db 0x10,0xff  # split: branch target inside data
L_00879b:
        db 0xff  # split: branch target inside data
L_00879c:
        db 0xff  # split: branch target inside data
L_00879d:
        db 0xff,0x00  # split: branch target inside data
L_00879f:
        db 0x00,0x00  # split: branch target inside data
L_0087a1:
        db 0x00,0x00  # split: branch target inside data
L_0087a3:
        db 0x3f  # split: branch target inside data
L_0087a4:
        db 0xfc  # split: branch target inside data
L_0087a5:
        db 0x00,0x00  # split: branch target inside data
L_0087a7:
        db 0xf3,0xcf  # split: branch target inside data
L_0087a9:
        db 0x00,0x03  # split: branch target inside data
L_0087ab:
        db 0xc3  # split: branch target inside data
L_0087ac:
        db 0xc3  # split: branch target inside data
L_0087ad:
        db 0xc0,0x0f,0xc3  # split: branch target inside data
L_0087b0:
        db 0xc3  # split: branch target inside data
L_0087b1:
        db 0xf0,0x0f,0x03,0xc0  # split: branch target inside data
L_0087b5:
        db 0xf0,0x3f  # split: branch target inside data
L_0087b7:
        db 0x03,0xc0  # split: branch target inside data
L_0087b9:
        db 0xfc  # split: branch target inside data
L_0087ba:
        db 0x3f  # split: branch target inside data
L_0087bb:
        db 0x03,0xc0  # split: branch target inside data
L_0087bd:
        db 0xfc  # split: branch target inside data
L_0087be:
        db 0x0f,0x03,0xc0  # split: branch target inside data
L_0087c1:
        db 0xf0,0x0f  # split: branch target inside data
L_0087c3:
        db 0xc3  # split: branch target inside data
L_0087c4:
        db 0xc3  # split: branch target inside data
L_0087c5:
        db 0xf0,0x03,0xc3  # split: branch target inside data
L_0087c8:
        db 0xc3  # split: branch target inside data
L_0087c9:
        db 0xc0,0x00,0xc3  # split: branch target inside data
L_0087cc:
        db 0xc3  # split: branch target inside data
L_0087cd:
        db 0x00,0x0f  # split: branch target inside data
L_0087cf:
        db 0xcf  # split: branch target inside data
L_0087d0:
        db 0xf3,0xf0,0x00,0x00  # split: branch target inside data
L_0087d4:
        db 0x00,0x00  # split: branch target inside data
L_0087d6:
        db 0xff  # split: branch target inside data
L_0087d7:
        db 0xff  # split: branch target inside data
L_0087d8:
        db 0xff  # split: branch target inside data
L_0087d9:
        db 0xff,0x04  # split: branch target inside data
L_0087db:
        db 0x10,0xff  # split: branch target inside data
L_0087dd:
        db 0xff  # split: branch target inside data
L_0087de:
        db 0xff  # split: branch target inside data
L_0087df:
        db 0xff,0x00  # split: branch target inside data
L_0087e1:
        db 0x00,0x00  # split: branch target inside data
L_0087e3:
        db 0x00,0x00  # split: branch target inside data
L_0087e5:
        db 0x3f  # split: branch target inside data
L_0087e6:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0087e9:
        db 0xf0,0xf0,0x00,0x03  # split: branch target inside data
L_0087ed:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_0087f0:
        db 0x00,0xf0  # split: branch target inside data
L_0087f2:
        db 0x3f  # split: branch target inside data
L_0087f3:
        db 0x00,0x00  # split: branch target inside data
L_0087f5:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_0087f9:
        db 0xf0,0x0f,0xc0,0x00  # split: branch target inside data
L_0087fd:
        db 0xf0,0x0f,0xc0,0x00  # split: branch target inside data
L_008801:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008805:
        db 0xf0,0x3f  # split: branch target inside data
L_008807:
        db 0x00,0x00  # split: branch target inside data
L_008809:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_00880c:
        db 0x00,0xf0  # split: branch target inside data
L_00880e:
        db 0x30,0x00  # split: branch target inside data
L_008810:
        db 0x03,0xfc  # split: branch target inside data
L_008812:
        db 0x3f  # split: branch target inside data
L_008813:
        db 0x00,0x00  # split: branch target inside data
L_008815:
        db 0x00,0x00  # split: branch target inside data
L_008817:
        db 0x00,0xff  # split: branch target inside data
L_008819:
        db 0xff  # split: branch target inside data
L_00881a:
        db 0xff  # split: branch target inside data
L_00881b:
        db 0xff,0x04  # split: branch target inside data
L_00881d:
        db 0x10,0xff  # split: branch target inside data
L_00881f:
        db 0xff  # split: branch target inside data
L_008820:
        db 0xff  # split: branch target inside data
L_008821:
        db 0xff,0x00  # split: branch target inside data
L_008823:
        db 0x00,0x00  # split: branch target inside data
L_008825:
        db 0x00,0x00  # split: branch target inside data
L_008827:
        db 0x03,0xc0  # split: branch target inside data
L_008829:
        db 0x00,0x00  # split: branch target inside data
L_00882b:
        db 0x0f,0xf0  # split: branch target inside data
L_00882d:
        db 0x00,0x00  # split: branch target inside data
L_00882f:
        db 0x3c,0x3c  # split: branch target inside data
L_008831:
        db 0x00,0x00  # split: branch target inside data
L_008833:
        db 0xfc  # split: branch target inside data
L_008834:
        db 0x3f  # split: branch target inside data
L_008835:
        db 0x00,0x00  # split: branch target inside data
L_008837:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_00883b:
        db 0xf0,0x0f,0xc0,0x03  # split: branch target inside data
L_00883f:
        db 0xf0,0x0f,0xc0,0x00  # split: branch target inside data
L_008843:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008847:
        db 0xfc  # split: branch target inside data
L_008848:
        db 0x3f  # split: branch target inside data
L_008849:
        db 0x00,0x00  # split: branch target inside data
L_00884b:
        db 0x3c,0x3c  # split: branch target inside data
L_00884d:
        db 0x00,0x00  # split: branch target inside data
L_00884f:
        db 0x0f,0xf0  # split: branch target inside data
L_008851:
        db 0x00,0x00  # split: branch target inside data
L_008853:
        db 0x03,0xc0  # split: branch target inside data
L_008855:
        db 0x00,0x00  # split: branch target inside data
L_008857:
        db 0x00,0x00  # split: branch target inside data
L_008859:
        db 0x00,0xff  # split: branch target inside data
L_00885b:
        db 0xff  # split: branch target inside data
L_00885c:
        db 0xff  # split: branch target inside data
L_00885d:
        db 0xff,0x04  # split: branch target inside data
L_00885f:
        db 0x10,0xff  # split: branch target inside data
L_008861:
        db 0xff  # split: branch target inside data
L_008862:
        db 0xff  # split: branch target inside data
L_008863:
        db 0xff,0x00  # split: branch target inside data
L_008865:
        db 0x00,0x00  # split: branch target inside data
L_008867:
        db 0x00,0x00  # split: branch target inside data
L_008869:
        db 0x0f,0xf0  # split: branch target inside data
L_00886b:
        db 0x00,0x00  # split: branch target inside data
L_00886d:
        db 0x3c,0x3c  # split: branch target inside data
L_00886f:
        db 0x00,0x00  # split: branch target inside data
L_008871:
        db 0xfc  # split: branch target inside data
L_008872:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008875:
        db 0x3c,0x0f  # split: branch target inside data
L_008877:
        db 0xc0,0x00,0x3c  # split: branch target inside data
L_00887a:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_00887d:
        db 0x3c,0x0f  # split: branch target inside data
L_00887f:
        db 0x00,0x00  # split: branch target inside data
L_008881:
        db 0x3c,0x3c  # split: branch target inside data
L_008883:
        db 0x00,0x00  # split: branch target inside data
L_008885:
        db 0x3c,0xf0  # split: branch target inside data
L_008887:
        db 0x00,0x00  # split: branch target inside data
L_008889:
        db 0x3c,0x00  # split: branch target inside data
L_00888b:
        db 0x00,0x00  # split: branch target inside data
L_00888d:
        db 0x3c,0x00  # split: branch target inside data
L_00888f:
        db 0x00,0x00  # split: branch target inside data
L_008891:
        db 0x3c,0x00  # split: branch target inside data
L_008893:
        db 0x00,0x00  # split: branch target inside data
L_008895:
        db 0xff,0xf0  # split: branch target inside data
L_008897:
        db 0x00,0x00  # split: branch target inside data
L_008899:
        db 0x00,0x00  # split: branch target inside data
L_00889b:
        db 0x00,0xff  # split: branch target inside data
L_00889d:
        db 0xff  # split: branch target inside data
L_00889e:
        db 0xff  # split: branch target inside data
L_00889f:
        db 0xff,0x04  # split: branch target inside data
L_0088a1:
        db 0x10,0xff  # split: branch target inside data
L_0088a3:
        db 0xff  # split: branch target inside data
L_0088a4:
        db 0xff  # split: branch target inside data
L_0088a5:
        db 0xfc  # split: branch target inside data
L_0088a6:
        db 0xff  # split: branch target inside data
L_0088a7:
        db 0xff  # split: branch target inside data
L_0088a8:
        db 0xff  # split: branch target inside data
L_0088a9:
        db 0xfc  # split: branch target inside data
L_0088aa:
        db 0xf3,0x33,0x33  # split: branch target inside data
L_0088ad:
        db 0x3c,0xf3  # split: branch target inside data
L_0088af:
        db 0x33,0x33  # split: branch target inside data
L_0088b1:
        db 0x3c,0xf3  # split: branch target inside data
L_0088b3:
        db 0x33,0x33  # split: branch target inside data
L_0088b5:
        db 0x3c,0xf3  # split: branch target inside data
L_0088b7:
        db 0x33,0x33  # split: branch target inside data
L_0088b9:
        db 0x3c,0xf3  # split: branch target inside data
L_0088bb:
        db 0x33,0x33  # split: branch target inside data
L_0088bd:
        db 0x3c,0xf3  # split: branch target inside data
L_0088bf:
        db 0x33,0x33  # split: branch target inside data
L_0088c1:
        db 0x3c,0xf3  # split: branch target inside data
L_0088c3:
        db 0x33,0x33  # split: branch target inside data
L_0088c5:
        db 0x3c,0xf3  # split: branch target inside data
L_0088c7:
        db 0x33,0x33  # split: branch target inside data
L_0088c9:
        db 0x3c,0xf3  # split: branch target inside data
L_0088cb:
        db 0x33,0x33  # split: branch target inside data
L_0088cd:
        db 0x3c,0xf3  # split: branch target inside data
L_0088cf:
        db 0x33,0x33  # split: branch target inside data
L_0088d1:
        db 0x3c,0xf3  # split: branch target inside data
L_0088d3:
        db 0x33,0x33  # split: branch target inside data
L_0088d5:
        db 0x3c,0xf3  # split: branch target inside data
L_0088d7:
        db 0x33,0x33  # split: branch target inside data
L_0088d9:
        db 0x3c,0xf3  # split: branch target inside data
L_0088db:
        db 0x33,0x33  # split: branch target inside data
L_0088dd:
        db 0x3c,0xf3  # split: branch target inside data
L_0088df:
        db 0x33,0x33  # split: branch target inside data
L_0088e1:
        db 0x3c,0x04  # split: branch target inside data
L_0088e3:
        db 0x10,0xff  # split: branch target inside data
L_0088e5:
        db 0xff  # split: branch target inside data
L_0088e6:
        db 0xff  # split: branch target inside data
L_0088e7:
        db 0xff,0x00  # split: branch target inside data
L_0088e9:
        db 0x00,0x00  # split: branch target inside data
L_0088eb:
        db 0x00,0x00  # split: branch target inside data
L_0088ed:
        db 0x3f  # split: branch target inside data
L_0088ee:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0088f1:
        db 0xf0,0xf0,0x00,0x03  # split: branch target inside data
L_0088f5:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_0088f8:
        db 0x00,0xf0  # split: branch target inside data
L_0088fa:
        db 0x3f  # split: branch target inside data
L_0088fb:
        db 0x00,0x00  # split: branch target inside data
L_0088fd:
        db 0xf0,0x3f  # split: branch target inside data
L_0088ff:
        db 0x00,0x00  # split: branch target inside data
L_008901:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_008904:
        db 0x00,0xf3  # split: branch target inside data
L_008906:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008909:
        db 0xf0,0x30,0x00  # split: branch target inside data
L_00890c:
        db 0x00,0xf0  # split: branch target inside data
L_00890e:
        db 0x3c,0x00  # split: branch target inside data
L_008910:
        db 0x00,0xf0  # split: branch target inside data
L_008912:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008915:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_008919:
        db 0xfc  # split: branch target inside data
L_00891a:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_00891d:
        db 0x00,0x00  # split: branch target inside data
L_00891f:
        db 0x00,0xff  # split: branch target inside data
L_008921:
        db 0xff  # split: branch target inside data
L_008922:
        db 0xff  # split: branch target inside data
L_008923:
        db 0xff,0x04  # split: branch target inside data
L_008925:
        db 0x10,0xff  # split: branch target inside data
L_008927:
        db 0xff  # split: branch target inside data
L_008928:
        db 0xff  # split: branch target inside data
L_008929:
        db 0xff,0x00  # split: branch target inside data
L_00892b:
        db 0x00,0x00  # split: branch target inside data
L_00892d:
        db 0x00,0x00  # split: branch target inside data
L_00892f:
        db 0x03,0xff  # split: branch target inside data
L_008931:
        db 0x00,0x00  # split: branch target inside data
L_008933:
        db 0x0f,0x3c  # split: branch target inside data
L_008935:
        db 0x00,0x00  # split: branch target inside data
L_008937:
        db 0x3c,0x0c  # split: branch target inside data
L_008939:
        db 0x00,0x00  # split: branch target inside data
L_00893b:
        db 0xff,0x00  # split: branch target inside data
L_00893d:
        db 0x00,0x00  # split: branch target inside data
L_00893f:
        db 0xff,0x00  # split: branch target inside data
L_008941:
        db 0x00,0x00  # split: branch target inside data
L_008943:
        db 0x3f  # split: branch target inside data
L_008944:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008947:
        db 0x0f,0xf0  # split: branch target inside data
L_008949:
        db 0x00,0x00  # split: branch target inside data
L_00894b:
        db 0x03,0xfc  # split: branch target inside data
L_00894d:
        db 0x00,0x00  # split: branch target inside data
L_00894f:
        db 0x00,0xff  # split: branch target inside data
L_008951:
        db 0x00,0x00  # split: branch target inside data
L_008953:
        db 0xc0,0xff,0x00  # split: branch target inside data
L_008956:
        db 0x00,0xf3  # split: branch target inside data
L_008958:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_00895b:
        db 0xff,0xc0  # split: branch target inside data
L_00895d:
        db 0x00,0x00  # split: branch target inside data
L_00895f:
        db 0x00,0x00  # split: branch target inside data
L_008961:
        db 0x00,0xff  # split: branch target inside data
L_008963:
        db 0xff  # split: branch target inside data
L_008964:
        db 0xff  # split: branch target inside data
L_008965:
        db 0xff,0x04  # split: branch target inside data
L_008967:
        db 0x10,0xff  # split: branch target inside data
L_008969:
        db 0xff  # split: branch target inside data
L_00896a:
        db 0xff  # split: branch target inside data
L_00896b:
        db 0xff,0x00  # split: branch target inside data
L_00896d:
        db 0x00,0x00  # split: branch target inside data
L_00896f:
        db 0x00,0x03  # split: branch target inside data
L_008971:
        db 0xff  # split: branch target inside data
L_008972:
        db 0xff,0xc0  # split: branch target inside data
L_008974:
        db 0x00,0xf3  # split: branch target inside data
L_008976:
        db 0xcf  # split: branch target inside data
L_008977:
        db 0x00,0x00  # split: branch target inside data
L_008979:
        db 0xc3  # split: branch target inside data
L_00897a:
        db 0xc3  # split: branch target inside data
L_00897b:
        db 0x00,0x00  # split: branch target inside data
L_00897d:
        db 0xc3  # split: branch target inside data
L_00897e:
        db 0xc3  # split: branch target inside data
L_00897f:
        db 0x00,0x00  # split: branch target inside data
L_008981:
        db 0x03,0xc0  # split: branch target inside data
L_008983:
        db 0x00,0x00  # split: branch target inside data
L_008985:
        db 0x03,0xc0  # split: branch target inside data
L_008987:
        db 0x00,0x00  # split: branch target inside data
L_008989:
        db 0x03,0xc0  # split: branch target inside data
L_00898b:
        db 0x00,0x00  # split: branch target inside data
L_00898d:
        db 0x03,0xc0  # split: branch target inside data
L_00898f:
        db 0x00,0x00  # split: branch target inside data
L_008991:
        db 0x03,0xc0  # split: branch target inside data
L_008993:
        db 0x00,0x00  # split: branch target inside data
L_008995:
        db 0x03,0xc0  # split: branch target inside data
L_008997:
        db 0x00,0x00  # split: branch target inside data
L_008999:
        db 0x03,0xc0  # split: branch target inside data
L_00899b:
        db 0x00,0x00  # split: branch target inside data
L_00899d:
        db 0x0f,0xf0  # split: branch target inside data
L_00899f:
        db 0x00,0x00  # split: branch target inside data
L_0089a1:
        db 0x00,0x00  # split: branch target inside data
L_0089a3:
        db 0x00,0xff  # split: branch target inside data
L_0089a5:
        db 0xff  # split: branch target inside data
L_0089a6:
        db 0xff  # split: branch target inside data
L_0089a7:
        db 0xff,0x04  # split: branch target inside data
L_0089a9:
        db 0x10,0xff  # split: branch target inside data
L_0089ab:
        db 0xff  # split: branch target inside data
L_0089ac:
        db 0xff  # split: branch target inside data
L_0089ad:
        db 0xff,0x00  # split: branch target inside data
L_0089af:
        db 0x00,0x00  # split: branch target inside data
L_0089b1:
        db 0x00,0x00  # split: branch target inside data
L_0089b3:
        db 0xfc  # split: branch target inside data
L_0089b4:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_0089b7:
        db 0x0c,0x0f  # split: branch target inside data
L_0089b9:
        db 0x00,0x00  # split: branch target inside data
L_0089bb:
        db 0x3c,0x0f  # split: branch target inside data
L_0089bd:
        db 0x00,0x00  # split: branch target inside data
L_0089bf:
        db 0xfc  # split: branch target inside data
L_0089c0:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0089c3:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_0089c7:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_0089cb:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_0089cf:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_0089d3:
        db 0xfc  # split: branch target inside data
L_0089d4:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0089d7:
        db 0x3c,0x0f  # split: branch target inside data
L_0089d9:
        db 0xc0,0x00,0x3c  # split: branch target inside data
L_0089dc:
        db 0x3c,0x00  # split: branch target inside data
L_0089de:
        db 0x00,0x0f  # split: branch target inside data
L_0089e0:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_0089e3:
        db 0x00,0x00  # split: branch target inside data
L_0089e5:
        db 0x00,0xff  # split: branch target inside data
L_0089e7:
        db 0xff  # split: branch target inside data
L_0089e8:
        db 0xff  # split: branch target inside data
L_0089e9:
        db 0xff,0x04  # split: branch target inside data
L_0089eb:
        db 0x10,0xff  # split: branch target inside data
L_0089ed:
        db 0xff  # split: branch target inside data
L_0089ee:
        db 0xff  # split: branch target inside data
L_0089ef:
        db 0xff,0x00  # split: branch target inside data
L_0089f1:
        db 0x00,0x00  # split: branch target inside data
L_0089f3:
        db 0x00,0x03  # split: branch target inside data
L_0089f5:
        db 0xf0,0xf0,0x00,0x00  # split: branch target inside data
L_0089f9:
        db 0x30,0x3c  # split: branch target inside data
L_0089fb:
        db 0x00,0x00  # split: branch target inside data
L_0089fd:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_008a01:
        db 0xf0,0x0f,0xc0,0x03  # split: branch target inside data
L_008a05:
        db 0xc0,0x03,0xc0  # split: branch target inside data
L_008a08:
        db 0x0f,0xc0,0x03  # split: branch target inside data
L_008a0b:
        db 0xf0,0x0f,0xc0,0x03  # split: branch target inside data
L_008a0f:
        db 0xf0,0x03,0xc0  # split: branch target inside data
L_008a12:
        db 0x03,0xc0  # split: branch target inside data
L_008a14:
        db 0x03,0xf0  # split: branch target inside data
L_008a16:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008a19:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008a1d:
        db 0x3c,0x3c  # split: branch target inside data
L_008a1f:
        db 0x00,0x00  # split: branch target inside data
L_008a21:
        db 0x0f,0xf0  # split: branch target inside data
L_008a23:
        db 0x00,0x00  # split: branch target inside data
L_008a25:
        db 0x00,0x00  # split: branch target inside data
L_008a27:
        db 0x00,0xff  # split: branch target inside data
L_008a29:
        db 0xff  # split: branch target inside data
L_008a2a:
        db 0xff  # split: branch target inside data
L_008a2b:
        db 0xff,0x04  # split: branch target inside data
L_008a2d:
        db 0x10,0xff  # split: branch target inside data
L_008a2f:
        db 0xff  # split: branch target inside data
L_008a30:
        db 0xff  # split: branch target inside data
L_008a31:
        db 0xff,0x00  # split: branch target inside data
L_008a33:
        db 0x00,0x00  # split: branch target inside data
L_008a35:
        db 0x00,0x0f  # split: branch target inside data
L_008a37:
        db 0xc3  # split: branch target inside data
L_008a38:
        db 0xc3  # split: branch target inside data
L_008a39:
        db 0xf0,0x00,0xc3  # split: branch target inside data
L_008a3c:
        db 0xc3  # split: branch target inside data
L_008a3d:
        db 0x00,0x03  # split: branch target inside data
L_008a3f:
        db 0xc3  # split: branch target inside data
L_008a40:
        db 0xc3  # split: branch target inside data
L_008a41:
        db 0xc0,0x0f,0xc3  # split: branch target inside data
L_008a44:
        db 0xc3  # split: branch target inside data
L_008a45:
        db 0xf0,0x0f,0x03,0xc0  # split: branch target inside data
L_008a49:
        db 0xf0,0x3f  # split: branch target inside data
L_008a4b:
        db 0x03,0xc0  # split: branch target inside data
L_008a4d:
        db 0xfc  # split: branch target inside data
L_008a4e:
        db 0x3f  # split: branch target inside data
L_008a4f:
        db 0x03,0xc0  # split: branch target inside data
L_008a51:
        db 0xfc  # split: branch target inside data
L_008a52:
        db 0x0f,0x03,0xc0  # split: branch target inside data
L_008a55:
        db 0xf0,0x0f  # split: branch target inside data
L_008a57:
        db 0xc3  # split: branch target inside data
L_008a58:
        db 0xc3  # split: branch target inside data
L_008a59:
        db 0xf0,0x03,0xc3  # split: branch target inside data
L_008a5c:
        db 0xc3  # split: branch target inside data
L_008a5d:
        db 0xc0,0x00,0xf3  # split: branch target inside data
L_008a60:
        db 0xcf  # split: branch target inside data
L_008a61:
        db 0x00,0x00  # split: branch target inside data
L_008a63:
        db 0x3f  # split: branch target inside data
L_008a64:
        db 0xfc  # split: branch target inside data
L_008a65:
        db 0x00,0x00  # split: branch target inside data
L_008a67:
        db 0x00,0x00  # split: branch target inside data
L_008a69:
        db 0x00,0xff  # split: branch target inside data
L_008a6b:
        db 0xff  # split: branch target inside data
L_008a6c:
        db 0xff  # split: branch target inside data
L_008a6d:
        db 0xff,0x04  # split: branch target inside data
L_008a6f:
        db 0x10,0xff  # split: branch target inside data
L_008a71:
        db 0xff  # split: branch target inside data
L_008a72:
        db 0xff  # split: branch target inside data
L_008a73:
        db 0xff,0x00  # split: branch target inside data
L_008a75:
        db 0x00,0x00  # split: branch target inside data
L_008a77:
        db 0x00,0x0f  # split: branch target inside data
L_008a79:
        db 0xf0,0x3f  # split: branch target inside data
L_008a7b:
        db 0xc0,0x03,0xc0  # split: branch target inside data
L_008a7e:
        db 0x0f,0x00,0x03  # split: branch target inside data
L_008a81:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_008a84:
        db 0x00,0xfc  # split: branch target inside data
L_008a86:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008a89:
        db 0xff,0xc0  # split: branch target inside data
L_008a8b:
        db 0x00,0x00  # split: branch target inside data
L_008a8d:
        db 0x3f  # split: branch target inside data
L_008a8e:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008a91:
        db 0x0f,0xf0  # split: branch target inside data
L_008a93:
        db 0x00,0x00  # split: branch target inside data
L_008a95:
        db 0x3f  # split: branch target inside data
L_008a96:
        db 0xfc  # split: branch target inside data
L_008a97:
        db 0x00,0x00  # split: branch target inside data
L_008a99:
        db 0xf0,0xfc  # split: branch target inside data
L_008a9b:
        db 0x00,0x03  # split: branch target inside data
L_008a9d:
        db 0xc0,0x3f,0x00  # split: branch target inside data
L_008aa0:
        db 0x0f,0x00,0x0f  # split: branch target inside data
L_008aa3:
        db 0x00,0x0f  # split: branch target inside data
L_008aa5:
        db 0xc0,0x3f,0xc0  # split: branch target inside data
L_008aa8:
        db 0x00,0x00  # split: branch target inside data
L_008aaa:
        db 0x00,0x00  # split: branch target inside data
L_008aac:
        db 0xff  # split: branch target inside data
L_008aad:
        db 0xff  # split: branch target inside data
L_008aae:
        db 0xff  # split: branch target inside data
L_008aaf:
        db 0xff,0x04  # split: branch target inside data
L_008ab1:
        db 0x10,0xff  # split: branch target inside data
L_008ab3:
        db 0xff  # split: branch target inside data
L_008ab4:
        db 0xff  # split: branch target inside data
L_008ab5:
        db 0xff,0x00  # split: branch target inside data
L_008ab7:
        db 0x00,0x00  # split: branch target inside data
L_008ab9:
        db 0x00,0x00  # split: branch target inside data
L_008abb:
        db 0x3c,0x3f  # split: branch target inside data
L_008abd:
        db 0xc0,0x00,0x0c  # split: branch target inside data
L_008ac0:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008ac3:
        db 0x3c,0x0f  # split: branch target inside data
L_008ac5:
        db 0x00,0x00  # split: branch target inside data
L_008ac7:
        db 0xfc  # split: branch target inside data
L_008ac8:
        db 0x0f,0x00,0x03  # split: branch target inside data
L_008acb:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_008acf:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008ad3:
        db 0xfc  # split: branch target inside data
L_008ad4:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008ad7:
        db 0x3c,0x0f  # split: branch target inside data
L_008ad9:
        db 0x00,0x00  # split: branch target inside data
L_008adb:
        db 0x0f,0xff,0x00  # split: branch target inside data
L_008ade:
        db 0x00,0xc0  # split: branch target inside data
L_008ae0:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008ae3:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_008ae6:
        db 0x00,0xff  # split: branch target inside data
L_008ae8:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008aeb:
        db 0x00,0x00  # split: branch target inside data
L_008aed:
        db 0x00,0xff  # split: branch target inside data
L_008aef:
        db 0xff  # split: branch target inside data
L_008af0:
        db 0xff  # split: branch target inside data
L_008af1:
        db 0xff,0x04  # split: branch target inside data
L_008af3:
        db 0x10,0xff  # split: branch target inside data
L_008af5:
        db 0xff  # split: branch target inside data
L_008af6:
        db 0xff  # split: branch target inside data
L_008af7:
        db 0xff,0x00  # split: branch target inside data
L_008af9:
        db 0x00,0x00  # split: branch target inside data
L_008afb:
        db 0x00,0x03  # split: branch target inside data
L_008afd:
        db 0xff  # split: branch target inside data
L_008afe:
        db 0xff,0x00  # split: branch target inside data
L_008b00:
        db 0x03,0xc0  # split: branch target inside data
L_008b02:
        db 0xfc  # split: branch target inside data
L_008b03:
        db 0x00,0x03  # split: branch target inside data
L_008b05:
        db 0x00,0xfc  # split: branch target inside data
L_008b07:
        db 0x00,0x00  # split: branch target inside data
L_008b09:
        db 0x03,0xf0  # split: branch target inside data
L_008b0b:
        db 0x00,0x00  # split: branch target inside data
L_008b0d:
        db 0x03,0xf0  # split: branch target inside data
L_008b0f:
        db 0x00,0x00  # split: branch target inside data
L_008b11:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008b14:
        db 0x00,0x0f  # split: branch target inside data
L_008b16:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008b19:
        db 0x3f  # split: branch target inside data
L_008b1a:
        db 0x03,0x00  # split: branch target inside data
L_008b1c:
        db 0x00,0x3f  # split: branch target inside data
L_008b1e:
        db 0x03,0x00  # split: branch target inside data
L_008b20:
        db 0x00,0xfc  # split: branch target inside data
L_008b22:
        db 0x03,0x00  # split: branch target inside data
L_008b24:
        db 0x00,0xfc  # split: branch target inside data
L_008b26:
        db 0x0f,0x00,0x03  # split: branch target inside data
L_008b29:
        db 0xff  # split: branch target inside data
L_008b2a:
        db 0xff,0xc0  # split: branch target inside data
L_008b2c:
        db 0x00,0x00  # split: branch target inside data
L_008b2e:
        db 0x00,0x00  # split: branch target inside data
L_008b30:
        db 0xff  # split: branch target inside data
L_008b31:
        db 0xff  # split: branch target inside data
L_008b32:
        db 0xff  # split: branch target inside data
L_008b33:
        db 0xff,0x04  # split: branch target inside data
L_008b35:
        db 0x10,0xff  # split: branch target inside data
L_008b37:
        db 0xff  # split: branch target inside data
L_008b38:
        db 0xff  # split: branch target inside data
L_008b39:
        db 0xff,0x00  # split: branch target inside data
L_008b3b:
        db 0x00,0x00  # split: branch target inside data
L_008b3d:
        db 0x03,0x00  # split: branch target inside data
L_008b3f:
        db 0x00,0x00  # split: branch target inside data
L_008b41:
        db 0x03,0x00  # split: branch target inside data
L_008b43:
        db 0x00,0x00  # split: branch target inside data
L_008b45:
        db 0x03,0x00  # split: branch target inside data
L_008b47:
        db 0x00,0x00  # split: branch target inside data
L_008b49:
        db 0x03,0x00  # split: branch target inside data
L_008b4b:
        db 0x00,0x00  # split: branch target inside data
L_008b4d:
        db 0x03,0x00  # split: branch target inside data
L_008b4f:
        db 0x00,0x00  # split: branch target inside data
L_008b51:
        db 0x03,0x00  # split: branch target inside data
L_008b53:
        db 0x00,0x00  # split: branch target inside data
L_008b55:
        db 0x03,0x00  # split: branch target inside data
L_008b57:
        db 0x00,0x00  # split: branch target inside data
L_008b59:
        db 0x03,0x00  # split: branch target inside data
L_008b5b:
        db 0x00,0x00  # split: branch target inside data
L_008b5d:
        db 0x03,0x00  # split: branch target inside data
L_008b5f:
        db 0x00,0x00  # split: branch target inside data
L_008b61:
        db 0x03,0x00  # split: branch target inside data
L_008b63:
        db 0x00,0x00  # split: branch target inside data
L_008b65:
        db 0x03,0x00  # split: branch target inside data
L_008b67:
        db 0x00,0x00  # split: branch target inside data
L_008b69:
        db 0x03,0x00  # split: branch target inside data
L_008b6b:
        db 0x00,0x00  # split: branch target inside data
L_008b6d:
        db 0x03,0x00  # split: branch target inside data
L_008b6f:
        db 0x00,0x00  # split: branch target inside data
L_008b71:
        db 0x03,0xff  # split: branch target inside data
L_008b73:
        db 0xff  # split: branch target inside data
L_008b74:
        db 0xff  # split: branch target inside data
L_008b75:
        db 0xff,0x04  # split: branch target inside data
L_008b77:
        db 0x10,0xff  # split: branch target inside data
L_008b79:
        db 0xff  # split: branch target inside data
L_008b7a:
        db 0xff  # split: branch target inside data
L_008b7b:
        db 0xff,0xc0  # split: branch target inside data
L_008b7d:
        db 0x00,0x00  # split: branch target inside data
L_008b7f:
        db 0x00,0xc0  # split: branch target inside data
L_008b81:
        db 0x00,0x00  # split: branch target inside data
L_008b83:
        db 0x00,0xc0  # split: branch target inside data
L_008b85:
        db 0x00,0x00  # split: branch target inside data
L_008b87:
        db 0x00,0xc0  # split: branch target inside data
L_008b89:
        db 0x00,0x00  # split: branch target inside data
L_008b8b:
        db 0x00,0xc0  # split: branch target inside data
L_008b8d:
        db 0x00,0x00  # split: branch target inside data
L_008b8f:
        db 0x00,0xc0  # split: branch target inside data
L_008b91:
        db 0x00,0x00  # split: branch target inside data
L_008b93:
        db 0x00,0xc0  # split: branch target inside data
L_008b95:
        db 0x00,0x00  # split: branch target inside data
L_008b97:
        db 0x00,0xc0  # split: branch target inside data
L_008b99:
        db 0x00,0x00  # split: branch target inside data
L_008b9b:
        db 0x00,0xc0  # split: branch target inside data
L_008b9d:
        db 0x00,0x00  # split: branch target inside data
L_008b9f:
        db 0x00,0xc0  # split: branch target inside data
L_008ba1:
        db 0x00,0x00  # split: branch target inside data
L_008ba3:
        db 0x00,0xc0  # split: branch target inside data
L_008ba5:
        db 0x00,0x00  # split: branch target inside data
L_008ba7:
        db 0x00,0xc0  # split: branch target inside data
L_008ba9:
        db 0x00,0x00  # split: branch target inside data
L_008bab:
        db 0x00,0xc0  # split: branch target inside data
L_008bad:
        db 0x00,0x00  # split: branch target inside data
L_008baf:
        db 0x00,0xc0  # split: branch target inside data
L_008bb1:
        db 0x00,0x00  # split: branch target inside data
L_008bb3:
        db 0x00,0xff  # split: branch target inside data
L_008bb5:
        db 0xff  # split: branch target inside data
L_008bb6:
        db 0xff  # split: branch target inside data
L_008bb7:
        db 0xff,0x04  # split: branch target inside data
L_008bb9:
        db 0x10,0x00  # split: branch target inside data
L_008bbb:
        db 0x00,0x00  # split: branch target inside data
L_008bbd:
        db 0x00,0x00  # split: branch target inside data
L_008bbf:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008bc2:
        db 0x00,0x0f  # split: branch target inside data
L_008bc4:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008bc7:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008bca:
        db 0x00,0x03  # split: branch target inside data
L_008bcc:
        db 0x00,0x00  # split: branch target inside data
L_008bce:
        db 0x00,0xff  # split: branch target inside data
L_008bd0:
        db 0xfc  # split: branch target inside data
L_008bd1:
        db 0x00,0x03  # split: branch target inside data
L_008bd3:
        db 0x3f  # split: branch target inside data
L_008bd4:
        db 0xf3,0x00,0x0c  # split: branch target inside data
L_008bd7:
        db 0x0f,0xc0,0xc0  # split: branch target inside data
L_008bda:
        db 0x03,0x03  # split: branch target inside data
L_008bdc:
        db 0x03,0x00  # split: branch target inside data
L_008bde:
        db 0x00,0xcf  # split: branch target inside data
L_008be0:
        db 0xcc  # split: branch target inside data
L_008be1:
        db 0x00,0x00  # split: branch target inside data
L_008be3:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008be6:
        db 0x00,0x3c  # split: branch target inside data
L_008be8:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008beb:
        db 0x30,0x30  # split: branch target inside data
L_008bed:
        db 0x00,0x00  # split: branch target inside data
L_008bef:
        db 0x30,0x30  # split: branch target inside data
L_008bf1:
        db 0x00,0x00  # split: branch target inside data
L_008bf3:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_008bf6:
        db 0x00,0x00  # split: branch target inside data
L_008bf8:
        db 0x00,0x00  # split: branch target inside data
L_008bfa:
        db 0x04,0x10  # split: branch target inside data
L_008bfc:
        db 0x00,0x00  # split: branch target inside data
L_008bfe:
        db 0x00,0x00  # split: branch target inside data
L_008c00:
        db 0x00,0x3f  # split: branch target inside data
L_008c02:
        db 0x00,0xc0  # split: branch target inside data
L_008c04:
        db 0x00,0x3f  # split: branch target inside data
L_008c06:
        db 0x00,0xc0  # split: branch target inside data
L_008c08:
        db 0x00,0x3f  # split: branch target inside data
L_008c0a:
        db 0x0f,0xfc,0x00  # split: branch target inside data
L_008c0d:
        db 0x0c,0x00  # split: branch target inside data
L_008c0f:
        db 0xc0,0x03,0xff  # split: branch target inside data
L_008c12:
        db 0xf0,0xc0,0x0c,0xff  # split: branch target inside data
L_008c16:
        db 0xcc  # split: branch target inside data
L_008c17:
        db 0xc0,0x0c,0x3f  # split: branch target inside data
L_008c1a:
        db 0x03,0xc0  # split: branch target inside data
L_008c1c:
        db 0x0c,0x0c  # split: branch target inside data
L_008c1e:
        db 0x00,0xc0  # split: branch target inside data
L_008c20:
        db 0x0c,0x3f  # split: branch target inside data
L_008c22:
        db 0x00,0x00  # split: branch target inside data
L_008c24:
        db 0x00,0xf3  # split: branch target inside data
L_008c26:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008c29:
        db 0xc0,0xc0,0x00  # split: branch target inside data
L_008c2c:
        db 0x00,0xc0  # split: branch target inside data
L_008c2e:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008c31:
        db 0xc0,0xc0,0x00  # split: branch target inside data
L_008c34:
        db 0x03,0xc0  # split: branch target inside data
L_008c36:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008c39:
        db 0x00,0x00  # split: branch target inside data
L_008c3b:
        db 0x00,0x04  # split: branch target inside data
L_008c3d:
        db 0x10,0x00  # split: branch target inside data
L_008c3f:
        db 0x00,0x00  # split: branch target inside data
L_008c41:
        db 0x00,0x00  # split: branch target inside data
L_008c43:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008c46:
        db 0x00,0x0f  # split: branch target inside data
L_008c48:
        db 0xc0,0x0c,0x00  # split: branch target inside data
L_008c4b:
        db 0x0f,0xc0,0x0c  # split: branch target inside data
L_008c4e:
        db 0x00,0x03  # split: branch target inside data
L_008c50:
        db 0x00,0x0c  # split: branch target inside data
L_008c52:
        db 0x00,0xff  # split: branch target inside data
L_008c54:
        db 0xfc  # split: branch target inside data
L_008c55:
        db 0x0c,0x33  # split: branch target inside data
L_008c57:
        db 0x3f  # split: branch target inside data
L_008c58:
        db 0xf3,0x3c,0x0c  # split: branch target inside data
L_008c5b:
        db 0x0f,0xc0,0xcc  # split: branch target inside data
L_008c5e:
        db 0x00,0x03  # split: branch target inside data
L_008c60:
        db 0x00,0x0c  # split: branch target inside data
L_008c62:
        db 0x00,0x0f  # split: branch target inside data
L_008c64:
        db 0xc0,0x0c,0x00  # split: branch target inside data
L_008c67:
        db 0x3c,0xf0  # split: branch target inside data
L_008c69:
        db 0x0c,0x00  # split: branch target inside data
L_008c6b:
        db 0x30,0x30  # split: branch target inside data
L_008c6d:
        db 0x0c,0x00  # split: branch target inside data
L_008c6f:
        db 0x30,0x30  # split: branch target inside data
L_008c71:
        db 0x0c,0x00  # split: branch target inside data
L_008c73:
        db 0x30,0x30  # split: branch target inside data
L_008c75:
        db 0x0c,0x00  # split: branch target inside data
L_008c77:
        db 0xf0,0x3c,0x0c  # split: branch target inside data
L_008c7a:
        db 0x00,0x00  # split: branch target inside data
L_008c7c:
        db 0x00,0x00  # split: branch target inside data
L_008c7e:
        db 0x04,0x10  # split: branch target inside data
L_008c80:
        db 0x00,0x00  # split: branch target inside data
L_008c82:
        db 0x00,0x00  # split: branch target inside data
L_008c84:
        db 0x00,0x3f  # split: branch target inside data
L_008c86:
        db 0x00,0x00  # split: branch target inside data
L_008c88:
        db 0x00,0x3f  # split: branch target inside data
L_008c8a:
        db 0x00,0x00  # split: branch target inside data
L_008c8c:
        db 0x00,0x3f  # split: branch target inside data
L_008c8e:
        db 0x00,0x00  # split: branch target inside data
L_008c90:
        db 0x00,0x0c  # split: branch target inside data
L_008c92:
        db 0x00,0x00  # split: branch target inside data
L_008c94:
        db 0x03,0xff  # split: branch target inside data
L_008c96:
        db 0xfc  # split: branch target inside data
L_008c97:
        db 0x00,0x0c  # split: branch target inside data
L_008c99:
        db 0xff,0xcc  # split: branch target inside data
L_008c9b:
        db 0x0c,0x0c  # split: branch target inside data
L_008c9d:
        db 0x3f  # split: branch target inside data
L_008c9e:
        db 0x0c,0x30  # split: branch target inside data
L_008ca0:
        db 0x0c,0x0c  # split: branch target inside data
L_008ca2:
        db 0x0c,0xc0  # split: branch target inside data
L_008ca4:
        db 0x0c,0x3f  # split: branch target inside data
L_008ca6:
        db 0x03,0x00  # split: branch target inside data
L_008ca8:
        db 0x0c,0x3f  # split: branch target inside data
L_008caa:
        db 0x0c,0xc0  # split: branch target inside data
L_008cac:
        db 0x00,0xf3  # split: branch target inside data
L_008cae:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008cb1:
        db 0xc0,0xc0,0x00  # split: branch target inside data
L_008cb4:
        db 0x00,0xc0  # split: branch target inside data
L_008cb6:
        db 0xc0,0x00,0x03  # split: branch target inside data
L_008cb9:
        db 0xc0,0xf0,0x00  # split: branch target inside data
L_008cbc:
        db 0x00,0x00  # split: branch target inside data
L_008cbe:
        db 0x00,0x00  # split: branch target inside data
L_008cc0:
        db 0x8c,0xd8  # split: branch target inside data
L_008cc2:
        db 0x8e,0xc0  # split: branch target inside data
L_008cc4:
        db 0x05,0x10,0x00  # split: branch target inside data
L_008cc7:
        db 0x8e,0xd0  # split: branch target inside data
L_008cc9:
        db 0xbc,0x00,0x00  # split: branch target inside data
L_008ccc:
        db 0x50  # split: branch target inside data
L_008ccd:
        db 0xb8,0x00,0x01  # split: branch target inside data
L_008cd0:
        db 0x50  # split: branch target inside data
L_008cd1:
        db 0x31,0xc0  # split: branch target inside data
L_008cd3:
        db 0x89,0xc3  # split: branch target inside data
L_008cd5:
        db 0x89,0xc1  # split: branch target inside data
L_008cd7:
        db 0x89,0xc2  # split: branch target inside data
L_008cd9:
        db 0x89,0xc5  # split: branch target inside data
L_008cdb:
        db 0x89,0xc6  # split: branch target inside data
L_008cdd:
        db 0x89,0xc7  # split: branch target inside data
L_008cdf:
        db 0xcb  # split: branch target inside data
L_008ce0:
        db 0x2e,0x43  # split: branch target inside data
L_008ce2:
        db 0xd9,0x83,0xbb,0x16  # split: branch target inside data
L_008ce6:
        db 0x53  # split: branch target inside data
L_008ce7:
        db 0xd3,0xaf,0x53,0x27  # split: branch target inside data
L_008ceb:
        db 0x50  # split: branch target inside data
L_008cec:
        db 0xba,0x94,0x53  # split: branch target inside data
L_008cef:
        db 0xba,0x21,0x52  # split: branch target inside data
L_008cf2:
        db 0xe1,0x53  # split: branch target inside data
L_008cf4:
        db 0xbb,0x66  # split: branch target inside data
L_008cf6:
        db 0x53  # split: branch target inside data
L_008cf7:
        db 0xd3,0xaf,0x53,0x27  # split: branch target inside data
L_008cfb:
        db 0xa0,0xe1,0x52  # split: branch target inside data
L_008cfe:
        db 0xbb,0x78,0x53  # split: branch target inside data
L_008d01:
        db 0xd3,0xaf,0x53,0x27  # split: branch target inside data
L_008d05:
        db 0xba,0xde,0x45  # split: branch target inside data
L_008d08:
        db 0x1f  # split: branch target inside data
L_008d09:
        db 0x52  # split: branch target inside data
L_008d0a:
        db 0xe7,0x5a  # split: branch target inside data
L_008d0c:
        db 0x9e  # split: branch target inside data
L_008d0d:
        db 0x72,0xe7  # split: branch target inside data
L_008d0f:
        db 0x53  # split: branch target inside data
L_008d10:
        db 0x9e  # split: branch target inside data
L_008d11:
        db 0x45  # split: branch target inside data
L_008d12:
        db 0xe1,0x53  # split: branch target inside data
L_008d14:
        db 0xbb,0x46,0x53  # split: branch target inside data
L_008d17:
        db 0xd3  # split: branch target inside data
L_008d18:
        db 0xaf,0x53,0x27  # split: branch target inside data
L_008d1b:
        db 0x80,0xe1,0x52  # split: branch target inside data
L_008d1e:
        db 0xbb,0x58  # split: branch target inside data
L_008d20:
        db 0x53  # split: branch target inside data
L_008d21:
        db 0xd3,0xaf,0x53  # split: branch target inside data
L_008d24:
        db 0x27  # split: branch target inside data
L_008d25:
        db 0x9a,0xba,0xde,0x53,0xba  # split: branch target inside data
L_008d2a:
        db 0xda,0x53,0xeb  # split: branch target inside data
L_008d2d:
        db 0x53  # split: branch target inside data
L_008d2e:
        db 0x53  # split: branch target inside data
L_008d2f:
        db 0xdd,0x93,0xe5,0x53  # split: branch target inside data
L_008d33:
        db 0xe8,0x2b,0x53  # split: branch target inside data
L_008d36:
        db 0x75,0xd8  # split: branch target inside data
L_008d38:
        db 0x54  # split: branch target inside data
L_008d39:
        db 0xd8,0x9b,0xe8,0x29  # split: branch target inside data
L_008d3d:
        db 0x53  # split: branch target inside data
L_008d3e:
        db 0x75,0xd8  # split: branch target inside data
L_008d40:
        db 0x54  # split: branch target inside data
L_008d41:
        db 0xdd,0x93,0xd8,0x8a  # split: branch target inside data
L_008d45:
        db 0xe3,0x50  # split: branch target inside data
L_008d47:
        db 0x75,0xdb  # split: branch target inside data
L_008d49:
        db 0x14,0x50  # split: branch target inside data
L_008d4b:
        db 0xe3,0xd3  # split: branch target inside data
L_008d4d:
        db 0x75,0xdb  # split: branch target inside data
L_008d4f:
        db 0x14,0x56  # split: branch target inside data
L_008d51:
        db 0xdf,0x98,0xdd,0x88  # split: branch target inside data
L_008d55:
        db 0xdd,0x90,0xe8,0x53  # split: branch target inside data
L_008d59:
        db 0x53  # split: branch target inside data
L_008d5a:
        db 0xea,0x56,0x53  # split: branch target inside data
L_008d5d:
        db 0x02,0xe2  # split: branch target inside data
L_008d5f:
        db 0x51  # split: branch target inside data
L_008d60:
        db 0xe6,0x55  # split: branch target inside data
L_008d62:
        db 0xe3,0x57  # split: branch target inside data
L_008d64:
        db 0xe7,0x57  # split: branch target inside data
L_008d66:
        db 0x9e  # split: branch target inside data
L_008d67:
        db 0x40  # split: branch target inside data
L_008d68:
        db 0x0a  # split: branch target inside data
L_008d69:
        db 0xd3  # split: branch target inside data
L_008d6a:
        db 0xaf  # split: branch target inside data
L_008d6b:
        db 0x53  # split: branch target inside data
L_008d6c:
        db 0x27  # split: branch target inside data
L_008d6d:
        db 0x56  # split: branch target inside data
L_008d6e:
        db 0xb1  # split: branch target inside data
L_008d6f:
        db 0xbe  # split: branch target inside data
L_008d70:
        db 0xb8,0x10,0xc3  # split: branch target inside data
L_008d73:
        db 0xeb,0x53  # split: branch target inside data
L_008d75:
        db 0x53  # split: branch target inside data
L_008d76:
        db 0xdd,0x93,0xe8,0x2b  # split: branch target inside data
L_008d7a:
        db 0x53  # split: branch target inside data
L_008d7b:
        db 0x75,0xd8  # split: branch target inside data
L_008d7d:
        db 0x54  # split: branch target inside data
L_008d7e:
        db 0xd8,0x9b,0xe8,0x29  # split: branch target inside data
L_008d82:
        db 0x53  # split: branch target inside data
L_008d83:
        db 0x75,0xd8  # split: branch target inside data
L_008d85:
        db 0x54  # split: branch target inside data
L_008d86:
        db 0xdd,0x93,0xd8,0x8a  # split: branch target inside data
L_008d8a:
        db 0xe3,0x51  # split: branch target inside data
L_008d8c:
        db 0x75,0xdb  # split: branch target inside data
L_008d8e:
        db 0x14,0x50  # split: branch target inside data
L_008d90:
        db 0xe3,0x49  # split: branch target inside data
L_008d92:
        db 0x75,0xdb  # split: branch target inside data
L_008d94:
        db 0x14,0x56  # split: branch target inside data
L_008d96:
        db 0xdf  # split: branch target inside data
L_008d97:
        db 0x98,0xdd,0x90  # split: branch target inside data
L_008d9a:
        db 0xe5,0x53  # split: branch target inside data
L_008d9c:
        db 0xe8,0x53,0x53  # split: branch target inside data
L_008d9f:
        db 0xea,0x56,0x53,0x02,0xe2  # split: branch target inside data
L_008da4:
        db 0xe8,0xe6,0x55  # split: branch target inside data
L_008da7:
        db 0xe3,0x57  # split: branch target inside data
L_008da9:
        db 0xe7,0x57  # split: branch target inside data
L_008dab:
        db 0x9e  # split: branch target inside data
L_008dac:
        db 0x40  # split: branch target inside data
L_008dad:
        db 0x0a,0xd3  # split: branch target inside data
L_008daf:
        db 0xaf  # split: branch target inside data
L_008db0:
        db 0x53  # split: branch target inside data
L_008db1:
        db 0x27  # split: branch target inside data
L_008db2:
        db 0x51  # split: branch target inside data
L_008db3:
        db 0xb1,0xbe  # split: branch target inside data
L_008db5:
        db 0x90  # split: branch target inside data
L_008db6:
        db 0xdf,0x98,0xdd,0x88  # split: branch target inside data
L_008dba:
        db 0xde  # split: branch target inside data
L_008dbb:
        db 0x45  # split: branch target inside data
L_008dbc:
        db 0x74  # split: branch target inside data
L_008dbd:
        db 0x52  # split: branch target inside data
L_008dbe:
        db 0xe7,0x5a  # split: branch target inside data
L_008dc0:
        db 0x9e  # split: branch target inside data
L_008dc1:
        db 0x72,0xe8  # split: branch target inside data
L_008dc3:
        db 0x53  # split: branch target inside data
L_008dc4:
        db 0x53  # split: branch target inside data
L_008dc5:
        db 0xdd,0x90,0xe8  # split: branch target inside data
L_008dc8:
        db 0x2b  # split: branch target inside data
L_008dc9:
        db 0x53  # split: branch target inside data
L_008dca:
        db 0x75,0xd8  # split: branch target inside data
L_008dcc:
        db 0x54  # split: branch target inside data
L_008dcd:
        db 0xd8,0x9b,0xe8,0x29  # split: branch target inside data
L_008dd1:
        db 0x53  # split: branch target inside data
L_008dd2:
        db 0x75,0xd8  # split: branch target inside data
L_008dd4:
        db 0x54  # split: branch target inside data
L_008dd5:
        db 0xdd,0x93,0xe3,0x51  # split: branch target inside data
L_008dd9:
        db 0x75,0xdb  # split: branch target inside data
L_008ddb:
        db 0x14,0x51  # split: branch target inside data
L_008ddd:
        db 0xe3,0x79  # split: branch target inside data
L_008ddf:
        db 0x75,0xdb  # split: branch target inside data
L_008de1:
        db 0x14,0x56  # split: branch target inside data
L_008de3:
        db 0xe7,0x1f  # split: branch target inside data
L_008de5:
        db 0x9e  # split: branch target inside data
L_008de6:
        db 0x72,0x5e  # split: branch target inside data
L_008de8:
        db 0x59  # split: branch target inside data
L_008de9:
        db 0x5e  # split: branch target inside data
L_008dea:
        db 0x59  # split: branch target inside data
L_008deb:
        db 0x03,0x1f  # split: branch target inside data
L_008ded:
        db 0x16  # split: branch target inside data
L_008dee:
        db 0x12,0x00  # split: branch target inside data
L_008df0:
        db 0x16  # split: branch target inside data
L_008df1:
        db 0x73,0x06  # split: branch target inside data
L_008df3:
        db 0x00,0x16,0x73,0x1c  # split: branch target inside data
L_008df7:
        db 0x01,0x1a  # split: branch target inside data
L_008df9:
        db 0x14,0x1a  # split: branch target inside data
L_008dfb:
        db 0x1d,0x12,0x1f  # split: branch target inside data
L_008dfe:
        db 0x73,0x17  # split: branch target inside data
L_008e00:
        db 0x1a,0x00  # split: branch target inside data
L_008e02:
        db 0x18,0x73,0x1c  # split: branch target inside data
L_008e05:
        db 0x1d,0x1f,0x0a  # split: branch target inside data
L_008e08:
        db 0x7d,0x5e  # split: branch target inside data
L_008e0a:
        db 0x59  # split: branch target inside data
L_008e0b:
        db 0x77,0x5e  # split: branch target inside data
L_008e0d:
        db 0x59  # split: branch target inside data
L_008e0e:
        db 0x5e  # split: branch target inside data
L_008e0f:
        db 0x59  # split: branch target inside data
L_008e10:
        db 0x03,0x1f  # split: branch target inside data
L_008e12:
        db 0x16  # split: branch target inside data
L_008e13:
        db 0x12,0x00  # split: branch target inside data
L_008e15:
        db 0x16  # split: branch target inside data
L_008e16:
        db 0x73  # split: branch target inside data
L_008e17:
        db 0x1a  # split: branch target inside data
L_008e18:
        db 0x1d,0x00,0x16  # split: branch target inside data
L_008e1b:
        db 0x01,0x07  # split: branch target inside data
L_008e1d:
        db 0x73,0x1c  # split: branch target inside data
L_008e1f:
        db 0x01,0x1a  # split: branch target inside data
L_008e21:
        db 0x14,0x1a  # split: branch target inside data
L_008e23:
        db 0x1d,0x12,0x1f  # split: branch target inside data
L_008e26:
        db 0x73,0x17  # split: branch target inside data
L_008e28:
        db 0x1a,0x00  # split: branch target inside data
L_008e2a:
        db 0x18,0x73,0x1a  # split: branch target inside data
L_008e2d:
        db 0x1d,0x73,0x17  # split: branch target inside data
L_008e30:
        db 0x01,0x1a  # split: branch target inside data
L_008e32:
        db 0x05,0x16,0x73  # split: branch target inside data
L_008e35:
        db 0x12,0x73,0x1c  # split: branch target inside data
L_008e38:
        db 0x01,0x73,0x11  # split: branch target inside data
L_008e3b:
        db 0x5e  # split: branch target inside data
L_008e3c:
        db 0x59  # split: branch target inside data
L_008e3d:
        db 0x07  # split: branch target inside data
L_008e3e:
        db 0x1b  # split: branch target inside data
L_008e3f:
        db 0x16,0x1d,0x73  # split: branch target inside data
L_008e42:
        db 0x03,0x01  # split: branch target inside data
L_008e44:
        db 0x16  # split: branch target inside data
L_008e45:
        db 0x00  # split: branch target inside data
L_008e46:
        db 0x00  # split: branch target inside data
L_008e47:
        db 0x73,0x12  # split: branch target inside data
L_008e49:
        db 0x1d,0x0a,0x73  # split: branch target inside data
L_008e4c:
        db 0x18,0x16,0x0a,0x73  # split: branch target inside data
L_008e50:
        db 0x07  # split: branch target inside data
L_008e51:
        db 0x1c,0x73  # split: branch target inside data
L_008e53:
        db 0x10,0x1c  # split: branch target inside data
L_008e55:
        db 0x1d,0x07,0x1a  # split: branch target inside data
L_008e58:
        db 0x1d,0x06,0x16  # split: branch target inside data
L_008e5b:
        db 0x73,0x7d  # split: branch target inside data
L_008e5d:
        db 0x73,0x7d  # split: branch target inside data
L_008e5f:
        db 0x73,0x7d  # split: branch target inside data
L_008e61:
        db 0x5e  # split: branch target inside data
L_008e62:
        db 0x59  # split: branch target inside data
L_008e63:
        db 0x77,0xdf  # split: branch target inside data
L_008e65:
        db 0x98  # split: branch target inside data
L_008e66:
        db 0xdd,0x88  # split: branch target inside data
L_008e68:
        db 0xdd,0x90  # split: branch target inside data
L_008e6a:
        db 0xde  # split: branch target inside data
L_008e6b:
        db 0x4d,0x4b  # split: branch target inside data
L_008e6d:
        db 0x50  # split: branch target inside data
L_008e6e:
        db 0xea,0x56,0x53,0x02,0xe2  # split: branch target inside data
L_008e73:
        db 0x52  # split: branch target inside data
L_008e74:
        db 0xe6,0x55  # split: branch target inside data
L_008e76:
        db 0xe3,0x52  # split: branch target inside data
L_008e78:
        db 0xe7,0x51  # split: branch target inside data
L_008e7a:
        db 0x9e  # split: branch target inside data
L_008e7b:
        db 0x40  # split: branch target inside data
L_008e7c:
        db 0x0a,0xd3  # split: branch target inside data
L_008e7e:
        db 0xaf  # split: branch target inside data
L_008e7f:
        db 0x53  # split: branch target inside data
L_008e80:
        db 0x27  # split: branch target inside data
L_008e81:
        db 0x56  # split: branch target inside data
L_008e82:
        db 0xb1,0xbe  # split: branch target inside data
L_008e84:
        db 0xba,0x7c,0xac  # split: branch target inside data
L_008e87:
        db 0x54  # split: branch target inside data
L_008e88:
        db 0x4c  # split: branch target inside data
L_008e89:
        db 0xd8,0xa0,0xd2,0x95  # split: branch target inside data
L_008e8d:
        db 0x53  # split: branch target inside data
L_008e8e:
        db 0x52  # split: branch target inside data
L_008e8f:
        db 0x7d,0x50  # split: branch target inside data
L_008e91:
        db 0x65,0x51  # split: branch target inside data
L_008e93:
        db 0x53  # split: branch target inside data
L_008e94:
        db 0x4d  # split: branch target inside data
L_008e95:
        db 0xdf,0x88,0xd0,0x90  # split: branch target inside data
L_008e99:
        db 0x43  # split: branch target inside data
L_008e9a:
        db 0xdf,0x9b,0xdd,0x93  # split: branch target inside data
L_008e9e:
        db 0xdd,0x8b,0xec,0x5b  # split: branch target inside data
L_008ea2:
        db 0x53  # split: branch target inside data
L_008ea3:
        db 0xfe  # split: branch target inside data
L_008ea4:
        db 0xf8  # split: branch target inside data
L_008ea5:
        db 0xfe  # split: branch target inside data
L_008ea6:
        db 0x50  # split: branch target inside data
L_008ea7:
        db 0x90  # split: branch target inside data
L_008ea8:
        db 0xf8  # split: branch target inside data
L_008ea9:
        db 0xfe  # split: branch target inside data
L_008eaa:
        db 0xd8,0x9b,0xfe,0x50  # split: branch target inside data
L_008eae:
        db 0x90  # split: branch target inside data
L_008eaf:
        db 0x08,0x0d  # split: branch target inside data
L_008eb1:
        db 0x5d  # split: branch target inside data
L_008eb2:
        db 0x9d  # split: branch target inside data
L_008eb3:
        db 0xe8,0x1a,0x00  # split: branch target inside data
L_008eb6:
        db 0x8e,0xdb  # split: branch target inside data
L_008eb8:
        db 0x8e,0xc3  # split: branch target inside data
L_008eba:
        db 0x8e,0xd0  # split: branch target inside data
L_008ebc:
        db 0x8b,0xe1  # split: branch target inside data
L_008ebe:
        db 0x8b,0xc5  # split: branch target inside data
L_008ec0:
        db 0x8b,0xce  # split: branch target inside data
L_008ec2:
        db 0xbb,0x00,0x00  # split: branch target inside data
L_008ec5:
        db 0x8b,0xd3  # split: branch target inside data
L_008ec7:
        db 0x8b,0xea  # split: branch target inside data
L_008ec9:
        db 0x8b  # split: branch target inside data
L_008eca:
        db 0xf2  # split: branch target inside data
L_008ecb:
        db 0x8b,0xfa  # split: branch target inside data
L_008ecd:
        db 0xe9,0xf7,0xfd  # split: branch target inside data
