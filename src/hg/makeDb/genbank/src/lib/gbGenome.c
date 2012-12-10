#include "gbGenome.h"
#include "gbRelease.h"
#include "gbDefs.h"
#include "localmem.h"


struct dbToSpecies
/* structure mapping database prefix to species (e.g. hg -> "Homo sapiens"). */
{
    char *dbPrefix;           /* prefix of database (e.g. hg) */
    char **names;             /* list of species name, terminate by null.
                               * first name is prefered. */
};

static char *hgNames[] = {"Homo sapiens", NULL};
static char *mmNames[] = {"Mus musculus", "Mus sp.", 
                          "Mus musculus bactrianus",
                          "Mus musculus castaneus",
                          "Mus musculus domesticus",
                          "Mus musculus molossinus",
                          "Mus musculus musculus",
                          "Mus musculus wagneri", 
                          "Mus musculus brevirostris",
                          NULL};
static char *rnNames[] = {"Rattus norvegicus", "Rattus sp.", NULL};
static char *choHofNames[] = {"Choloepus hoffmanni", NULL};
static char *chrPicNames[] = {"Chrysemys picta", "Chrysemys picta marginata", "Chrysemys picta bellii", NULL};
static char *ciNames[] = {"Ciona intestinalis", NULL};
static char *cioSavNames[] = {"Ciona savignyi", NULL};
static char *strPurNames[] = {"Strongylocentrotus purpuratus", NULL};
static char *nemVecNames[] = {"Nematostella vectensis", NULL};
static char *frNames[] = {"Takifugu rubripes", NULL};
static char *dmNames[] = {"Drosophila melanogaster", "Drosophila sp.", NULL};
static char *dpNames[] = {"Drosophila pseudoobscura", NULL};
static char *sacCerNames[] = {"Saccharomyces cerevisiae", NULL};
static char *panTroNames[] = {"Pan troglodytes", "Pan troglodytes troglodytes", 
                              "Pan troglodytes verus", NULL};
static char *gorGorNames[] = {"Gorilla gorilla", "Gorilla gorilla gorilla", "Gorilla gorilla uellensis", "Gorilla gorilla diehli", "Gorilla gorilla graueri", "Gorilla berengi", NULL};
static char *papAnuNames[] = {"Papio anubis", "Papio hamadryas", "Papio hamadryas hamadryas", NULL};
static char *papHamNames[] = {"Papio hamadryas", "Papio hamadryas hamadryas", "Papio anubis", NULL};
static char *ponAbeNames[] = {"Pongo abelii", "Pongo pygmaeus",
		"Pongo pygmaeus pygmaeus", "Pongo pygmaeus abelii", NULL};
static char *rheMacNames[] = {"Macaca mulatta", NULL};
static char *monDomNames[] = {"Monodelphis domestica", NULL};
static char *galGalNames[] = {"Gallus gallus", "Gallus sp.", NULL};
static char *ceNames[] = {"Caenorhabditis elegans", NULL};
static char *cbNames[] = {"Caenorhabditis briggsae", NULL};
static char *caeRemNames[] = {"Caenorhabditis remanei", NULL};
static char *caeJapNames[] = {"Caenorhabditis japonica", NULL};
static char *caePbNames[] = {"Caenorhabditis brenneri", NULL};
static char *haeConNames[] = {"Haemonchus contortus", NULL};
static char *melHapNames[] = {"Meloidogyne hapla", NULL};
static char *melIncNames[] = {"Meloidogyne incognita", NULL};
static char *bruMalNames[] = {"Brugia malayi", NULL};
static char *calJacNames[] = {"Callithrix jacchus", NULL};
static char *danRerNames[] = {"Danio rerio", NULL};
static char *echTelNames[] = {"Echinops telfairi", NULL};
static char *oryCunNames[] = {"Oryctolagus cuniculus", NULL};
static char *cavPorNames[] = {"Cavia porcellus", NULL};
static char *loxAfrNames[] = {"Loxodonta africana", NULL};
static char *macEugNames[] = {"Macropus eugenii", NULL};
static char *triManNames[] = {"Trichechus manatus",
				"Trichechus manatus latirostris", NULL};
static char *dasNovNames[] = {"Dasypus novemcinctus", NULL};
static char *ailMelNames[] = {"Ailuropoda melanoleuca", NULL};
static char *canFamNames[] = {"Canis familiaris", "Canis sp.",
                              "Canis lupus familiaris",
			      "Canis lupus", NULL};
static char *felCatNames[] = {"Felis catus", NULL};
static char *droYakNames[] = {"Drosophila yakuba", NULL};
static char *droAnaNames[] = {"Drosophila ananassae", NULL};
static char *droMojNames[] = {"Drosophila mojavensis", NULL};
static char *droVirNames[] = {"Drosophila virilis", NULL};
static char *droEreNames[] = {"Drosophila erecta", NULL};
static char *droSimNames[] = {"Drosophila simulans", NULL};
static char *droGriNames[] = {"Drosophila grimshawi", NULL};
static char *droPerNames[] = {"Drosophila persimilis", NULL};
static char *droSecNames[] = {"Drosophila sechellia", NULL};
static char *anoGamNames[] = {"Anopheles gambiae", NULL};
static char *apiMelNames[] = {"Apis mellifera", NULL};
static char *triCasNames[] = {"Tribolium castaneum", NULL};
static char *tetNigNames[] = {"Tetraodon nigroviridis", NULL};
static char *bosTauNames[] = {"Bos taurus", NULL};
static char *xenTroNames[] = {"Xenopus tropicalis", 
                              "Xenopus (Silurana) tropicalis", NULL};
static char *anoCarNames[] = {"Anolis carolinensis", NULL};
static char *gasAcuNames[] = {"Gasterosteus aculeatus", NULL};
static char *oryLatNames[] = {"Oryzias latipes", NULL};
static char *chiLanNames[] = {"Chinchilla lanigera", NULL};
static char *equCabNames[] = {"Equus caballus", NULL};
static char *cerSimNames[] = {"Ceratotherium simum", "Ceratotherium simum simum", "Ceratotherium simum cottoni", NULL};
static char *oviAriNames[] = {"Ovis aries", NULL};
static char *susScrNames[] = {"Sus scrofa", "Sus scrofa coreanus", "Sus scrofa domesticus",
                              "Sus scrofa domestica", "Sus scrofa ussuricus", NULL};
static char *ornAnaNames[] = {"Ornithorhynchus anatinus", NULL};
static char *petMarNames[] = {"Petromyzon marinus", NULL};
static char *braFloNames[] = {"Branchiostoma floridae", "Branchiostoma belcheri", "Branchiostoma belcheri tsingtauense", "Branchiostoma californiense", "Branchiostoma japonicum", "Branchiostoma lanceolatum", NULL};
static char *priPacNames[] = {"Pristionchus pacificus", NULL};
static char *aplCalNames[] = {"Aplysia californica", NULL};
/* hypothetical ancestor, will never match native */
static char *canHgNames[] = {"Boreoeutheria ancestor", NULL};
static char *taeGutNames[] = {"Taeniopygia guttata", NULL};
static char *nomLeuNames[] = {"Nomascus leucogenys", NULL};
static char *myoLucNames[] = {"Myotis lucifugus", NULL};
static char *melGalNames[] = {"Meleagris gallopavo", NULL};
char *allMisNames[] = {"Alligator mississippiensis", NULL};
static char *hetGlaNames[] = {"Heterocephalus glaber", NULL};
static char *sarHarNames[] = {"Sarcophilus harrisii", NULL};
static char *dipOrdNames[] = {"Dipodomys ordii", "Dipodomys merriami", "Dipodomys spectabilis", NULL};
static char *otoGarNames[] = {"Otolemur garnettii", NULL};
static char *turTruNames[] = {"Tursiops truncatus", NULL};
static char *eriEurNames[] = {"Erinaceus europaeus", NULL};
static char *gadMorNames[] = {"Gadus morhua", NULL};
static char *latChaNames[] = {"Latimeria chalumnae", NULL};
static char *geoForNames[] = {"Geospiza fortis", NULL};
static char *melUndNames[] = {"Melopsittacus undulatus", NULL};
static char *micMurNames[] = {"Microcebus murinus", NULL};
static char *ochPriNames[] = {"Ochotona princeps", NULL};
static char *oreNilNames[] = {"Oreochromis niloticus", NULL};
static char *proCapNames[] = {"Procavia capensis", NULL};
static char *pteVamNames[] = {"Pteropus vampyrus", NULL};
static char *saiBolNames[] = {"Saimiri boliviensis", "Saimiri boliviensis boliviensis", NULL};
static char *sorAraNames[] = {"Sorex unguiculatus", NULL};
static char *speTriNames[] = {"Spermophilus tridecemlineatus", "Ictidomys tridecemlineatus", NULL};
static char *tarSyrNames[] = {"Tarsius syrichta", NULL};
static char *tupBelNames[] = {"Tupaia belangeri", NULL};
static char *vicPacNames[] = {"Vicugna pacos", NULL};

static char *endNames[] = {NULL};

static struct dbToSpecies dbToSpeciesMap[] = {
    {"hg", hgNames},
    {"mm", mmNames},
    {"rn", rnNames},
    {"choHof", choHofNames},
    {"chrPic", chrPicNames},
    {"ci", ciNames},
    {"cioSav", cioSavNames},
    {"fr", frNames},
    {"dm", dmNames},
    {"dp", dpNames},
    {"sacCer", sacCerNames},
    {"panTro", panTroNames},
    {"gorGor", gorGorNames},
    {"papAnu", papAnuNames},
    {"papHam", papHamNames},
    {"ponAbe", ponAbeNames},
    {"rheMac", rheMacNames},
    {"monDom", monDomNames},
    {"galGal", galGalNames},
    {"ce", ceNames},
    {"cb", cbNames},
    {"caeRem", caeRemNames},
    {"caeJap", caeJapNames},
    {"caePb", caePbNames},
    {"haeCon", haeConNames},
    {"melHap", melHapNames},
    {"melInc", melIncNames},
    {"bruMal", bruMalNames},
    {"caeRei", caeRemNames}, /* db spelling mistake, should be Rem */
    {"calJac", calJacNames},
    {"danRer", danRerNames},
    {"ailMel", ailMelNames},
    {"canFam", canFamNames},
    {"felCat", felCatNames},
    {"loxAfr", loxAfrNames},
    {"macEug", macEugNames},
    {"triMan", triManNames},
    {"dasNov", dasNovNames},
    {"echTel", echTelNames},
    {"oryCun", oryCunNames},
    {"cavPor", cavPorNames},
    {"equCab", equCabNames},
    {"cerSim", cerSimNames},
    {"oviAri", oviAriNames},
    {"susScr", susScrNames},
    {"droYak", droYakNames},
    {"droAna", droAnaNames},
    {"droMoj", droMojNames},
    {"droVir", droVirNames},
    {"droEre", droEreNames},
    {"droSim", droSimNames},
    {"droGri", droGriNames},
    {"droPer", droPerNames},
    {"droSec", droSecNames},
    {"anoGam", anoGamNames},
    {"apiMel", apiMelNames},
    {"triCas", triCasNames},
    {"tetNig", tetNigNames},
    {"bosTau", bosTauNames},
    {"xenTro", xenTroNames},
    {"anoCar", anoCarNames},
    {"gasAcu", gasAcuNames},
    {"oryLat", oryLatNames},
    {"chiLan", chiLanNames},
    {"ornAna", ornAnaNames},
    {"petMar", petMarNames},
    {"braFlo", braFloNames},
    {"priPac", priPacNames},
    {"canHg",  canHgNames},
    {"strPur", strPurNames},
    {"aplCal", aplCalNames},
    {"nemVec", nemVecNames},
    {"taeGut", taeGutNames},
    {"nomLeu", nomLeuNames},
    {"myoLuc", myoLucNames},
    {"melGal", melGalNames},
    {"allMis", allMisNames},
    {"hetGla", hetGlaNames},
    {"sarHar", sarHarNames},
    {"dipOrd", dipOrdNames},
    {"otoGar", otoGarNames},
    {"turTru", turTruNames},
    {"eriEur", eriEurNames},
    {"gadMor", gadMorNames},
    {"latCha", latChaNames},
    {"geoFor", geoForNames},
    {"melUnd", melUndNames},
    {"micMur", micMurNames},
    {"ochPri", ochPriNames},
    {"oreNil", oreNilNames},
    {"proCap", proCapNames},
    {"pteVam", pteVamNames},
    {"saiBol", saiBolNames},
    {"sorAra", sorAraNames},
    {"speTri", speTriNames},
    {"tarSyr", tarSyrNames},
    {"tupBel", tupBelNames},
    {"vicPac", vicPacNames},
    {NULL, endNames}
};

struct gbGenome* gbGenomeNew(char* database)
/* create a new gbGenome object */
{
struct dbToSpecies* dbMap;
struct gbGenome* genome;

for (dbMap = dbToSpeciesMap; dbMap->dbPrefix != NULL; dbMap++)
    {
    if (startsWith(dbMap->dbPrefix, database))
        break;
    }
if (dbMap->dbPrefix == NULL)
    errAbort("no species defined for database \"%s\"; edit %s to add definition",
             database, __FILE__);

AllocVar(genome);
genome->database = cloneString(database);
genome->organism = dbMap->names[0];
genome->dbMap = dbMap;
return genome;
}

static struct dbToSpecies *speciesSearch(char* organism)
/* search by species name */
{
int i, j;
for (i = 0; dbToSpeciesMap[i].dbPrefix != NULL; i++)
    {
    struct dbToSpecies* dbMap = &(dbToSpeciesMap[i]);
    for (j = 0; dbMap->names[j] != NULL; j++)
        {
        if (sameString(dbMap->names[j], organism))
            return dbMap;
        }
    }
return NULL;
}

char* gbGenomePreferedOrgName(char* organism)
/* determine the prefered organism name, if this organism is known,
 * otherwise NULL.  Used for sanity checks. */
{
/* caching last found helps speed search, since entries tend to be groups,
 * especially ESTs.  NULL is a valid cache entry, so need flag */
static boolean cacheEmpty = TRUE;
static struct dbToSpecies* dbMapCache = NULL;
static char organismCache[256];

if (cacheEmpty || !sameString(organism, organismCache))
    {
    dbMapCache = speciesSearch(organism);
    strcpy(organismCache, organism);
    cacheEmpty = FALSE;
    }

if (dbMapCache == NULL)
    return NULL;
else
    return dbMapCache->names[0];
}

unsigned gbGenomeOrgCat(struct gbGenome* genome, char* organism)
/* Compare a species to the one associated with this genome, returning
 * GB_NATIVE or GB_XENO, or 0 if genome is null. */
{
int i;
if (genome == NULL)
    return 0;
for (i = 0; genome->dbMap->names[i] != NULL; i++)
    {
    if (sameString(organism, genome->dbMap->names[i]))
        return GB_NATIVE;
    }

return GB_XENO;
}

void gbGenomeFree(struct gbGenome** genomePtr)
/* free a genome object */
{
struct gbGenome* genome = *genomePtr;
if (genome != NULL)
    {
    free(genome->database);
    *genomePtr = NULL;
    }
}

/*
 * Local Variables:
 * c-file-style: "jkent-c"
 * End:
 */

