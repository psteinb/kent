#!/bin/sh

if [ $# -ne 1 ]; then
    echo "usage: ./commonNames.sh <Nway.nh>"
    echo "Renames the names from UCSC database names to common names."
    echo "This depends upon the exact UCSC database names present in the"
    echo ".nh file.  Last updated to work with the 96way.nh file."
    exit 255
fi

export F=$1

/cluster/bin/phast/tree_doctor \
-r \
"hg19 -> Human ;
panTro3 -> Chimp ;
panTro4 -> Chimp ;
panPan1 -> Bonobo ;
gorGor3 -> Gorilla ;
ponAbe2 -> Orangutan ;
rheMac2 -> Rhesus ;
rheMac3 -> Rhesus ;
nomLeu1 -> Gibbon ;
nomLeu3 -> Gibbon ;
papHam1 -> Baboon ;
saiBol1 -> Squirrel_monkey ;
calJac3 -> Marmoset ;
tarSyr1 -> Tarsier ;
micMur1 -> Mouse_lemur ;
otoGar1 -> Bushbaby ;
otoGar3 -> Bushbaby ;
tupChi1 -> Chinese_tree_shrew ;
tupBel1 -> TreeShrew ;
criGri1 -> Chinese_hamster ;
mm9 -> Mouse ;
mm10 -> Mouse ;
rn4 -> Rat ;
rn5 -> Rat ;
octDeg1 -> brush_tailed_rat ;
dipOrd1 -> Kangaroo_rat ;
micOch1 -> Prairie_Vole ;
hetGla1 -> Mole_rat ;
hetGla2 -> Mole_rat ;
cavPor3 -> Guinea_Pig ;
jacJac1 -> lesser_Egyptian_jerboa ;
speTri1 -> Squirrel ;
speTri2 -> Squirrel ;
musFur1 -> Ferret ;
chiLan1 -> Chinchilla ;
oryCun2 -> Rabbit ;
ochPri2 -> Pika ;
ochPri3 -> Pika ;
susScr2 -> Pig ;
susScr3 -> Pig ;
vicPac1 -> Alpaca ;
turTru1 -> Dolphin ;
turTru2 -> Dolphin ;
oviAri1 -> Sheep ;
oviAri3 -> Sheep ;
capHir1 -> Goat ;
camFer1 -> Bactrian_camel ;
bosTau6 -> Cow ;
bosTau7 -> Cow ;
equCab2 -> Horse ;
cerSim1 -> White_rhinoceros ;
felCat4 -> Cat ;
felCat5 -> Cat ;
canFam2 -> Dog ;
canFam3 -> Dog ;
ailMel1 -> Panda ;
myoDav1 -> David_s_Myotis ;
myoLuc1 -> Microbat ;
myoLuc2 -> Microbat ;
pteAle1 -> Black_Flying_fox ;
pteVam1 -> Megabat ;
eptFus1 -> Big_brown_bat ;
odoRosDiv1 -> Pacific_walrus ;
eriEur1 -> Hedgehog ;
eriEur2 -> Hedgehog ;
conCri1 -> Star_nosed_mole ;
sorAra1 -> Shrew ;
sorAra2 -> Shrew ;
loxAfr3 -> Elephant ;
eleEdw1 -> Cape_elephant_shrew ;
oryAfe1 -> Aardvark ;
chrAsi1 -> Cape_golden_mole ;
orcOrc1 -> Killer_whale ;
proCap1 -> Rock_hyrax ;
echTel1 -> Tenrec ;
echTel2 -> Tenrec ;
dasNov2 -> Armadillo ;
dasNov3 -> Armadillo ;
triMan1 -> Manatee ;
choHof1 -> Sloth ;
macEug2 -> Wallaby ;
sarHar1 -> Tasmanian_devil ;
monDom5 -> Opossum ;
ornAna1 -> Platypus ;
colLiv1 -> Rock_pigeon ;
pseHum1 -> Tibetan_ground_jay ;
geoFor1 -> Medium_ground_finch ;
ficAlb1 -> Collared_flycatcher ;
amaVit1 -> Puerto_Rican_parrot ;
falChe1 -> Saker_falcon ;
falPer1 -> Peregrine_falcon ;
melUnd1 -> Budgerigar ;
melGal1 -> Turkey ;
galGal3 -> Chicken ;
galGal4 -> Chicken ;
taeGut1 -> Zebra_finch ;
taeGut2 -> Zebra_finch ;
allMis1 -> Alligator ;
croPor1 -> Crocodile ;
latCha1 -> Coelacanth ;
anoCar2 -> Lizard ;
pelSin1 -> Soft_shell_Turtle ;
chrPic1 -> Painted_turtle ;
xenTro3 -> X_tropicalis ;
xenTro4 -> X_tropicalis ;
xenTro7 -> X_tropicalis ;
lepOcu1 -> Spotted_Gar ;
tetNig1 -> Tetraodon ;
tetNig2 -> Tetraodon ;
fr2 -> Fugu ;
fr3 -> Fugu ;
oreNil1 -> Tilapia ;
oreNil2 -> Tilapia ;
hapBur1 -> Burton_s_mouthbreeder ;
neoBri1 -> Princess_of_Burundi ;
mayZeb1 -> Maylandia_zebra ;
punNye1 -> Pundamilia_nyererei ;
gasAcu1 -> Stickleback ;
oryLat2 -> Medaka ;
xipMac1 -> Southern_platyfish ;
danRer7 -> Zebrafish ;
gadMor1 -> Atlantic_cod ;
petMar1 -> Lamprey ;
petMar2 -> Lamprey ;" \
	${F} | sed -e "s/X_trop/X._trop/; s/Burton_s/Burton's/; s/David_s/David's/;"
