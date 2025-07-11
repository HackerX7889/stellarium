/*
 * Stellarium
 * Copyright (C) 2002 Fabien Chereau
 * Copyright (C) 2010 Bogdan Marinov
 * Copyright (C) 2011 Alexander Wolf
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#include <execution> // must be included before Qt because some versions of libtbb use "emit" identifier for their needs

#include "SolarSystem.hpp"
#include "StelTexture.hpp"
#include "EphemWrapper.hpp"
#include "Orbit.hpp"

#include "StelProjector.hpp"
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelTextureMgr.hpp"
#include "StelObjectMgr.hpp"
#include "StelLocaleMgr.hpp"
#include "StelSkyCultureMgr.hpp"
#include "StelFileMgr.hpp"
#include "StelModuleMgr.hpp"
#include "StelIniParser.hpp"
#include "Planet.hpp"
#include "MinorPlanet.hpp"
#include "Comet.hpp"
#include "StelMainView.hpp"

#include "StelSkyDrawer.hpp"
#include "StelUtils.hpp"
#include "StelPainter.hpp"
#include "TrailGroup.hpp"

#include "AstroCalcDialog.hpp"
#include "StelObserver.hpp"

#include <algorithm>

#include <QTextStream>
#include <QSettings>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QMultiMap>
#include <QMapIterator>
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QtConcurrent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>


SolarSystem::SolarSystem() : StelObjectModule()
	, shadowPlanetCount(0)
	, earthShadowEnlargementDanjon(false)
	, flagMoonScale(false)
	, moonScale(1.0)
	, flagMinorBodyScale(false)
	, minorBodyScale(1.0)
	, flagPlanetScale(false)
	, planetScale(1.0)
	, flagSunScale(false)
	, sunScale(1.0)
	, labelsAmount(false)
	, flagPermanentSolarCorona(true)
	, flagOrbits(false)
	, flagLightTravelTime(true)
	, flagUseObjModels(false)
	, flagShowObjSelfShadows(true)
	, flagShow(false)
	, flagPointer(false)
	, flagIsolatedTrails(true)
	, numberIsolatedTrails(0)
	, maxTrailPoints(5000)
	, maxTrailTimeExtent(1)
	, trailsThickness(1)
	, flagIsolatedOrbits(true)
	, flagPlanetsOrbits(false)
	, flagPlanetsOrbitsOnly(false)
	, flagOrbitsWithMoons(false)
	, ephemerisMarkersDisplayed(true)
	, ephemerisDatesDisplayed(false)
	, ephemerisMagnitudesDisplayed(false)
	, ephemerisHorizontalCoordinates(false)
	, ephemerisLineDisplayed(false)
	, ephemerisAlwaysOn(false)
	, ephemerisNow(false)
	, ephemerisLineThickness(1)
	, ephemerisSkipDataDisplayed(false)
	, ephemerisSkipMarkersDisplayed(false)
	, ephemerisDataStep(1)
	, ephemerisDataLimit(1)
	, ephemerisSmartDatesDisplayed(true)
	, ephemerisScaleMarkersDisplayed(false)
	, ephemerisGenericMarkerColor(Vec3f(1.0f, 1.0f, 0.0f))
	, ephemerisSecondaryMarkerColor(Vec3f(0.7f, 0.7f, 1.0f))
	, ephemerisSelectedMarkerColor(Vec3f(1.0f, 0.7f, 0.0f))
	, ephemerisMercuryMarkerColor(Vec3f(1.0f, 1.0f, 0.0f))
	, ephemerisVenusMarkerColor(Vec3f(1.0f, 1.0f, 1.0f))
	, ephemerisMarsMarkerColor(Vec3f(1.0f, 0.0f, 0.0f))
	, ephemerisJupiterMarkerColor(Vec3f(0.3f, 1.0f, 1.0f))
	, ephemerisSaturnMarkerColor(Vec3f(0.0f, 1.0f, 0.0f))
	, allTrails(Q_NULLPTR)
	, conf(StelApp::getInstance().getSettings())
	, extraThreads(0)
	, nbMarkers(0)
	, vao(new QOpenGLVertexArrayObject)
	, vbo(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer))
	, markerMagThreshold(15.)
	, computePositionsAlgorithm(conf->value("devel/compute_positions_algorithm", 2).toInt())
{
	planetNameFont.setPixelSize(StelApp::getInstance().getScreenFontSize());
	connect(&StelApp::getInstance(), SIGNAL(screenFontSizeChanged(int)), this, SLOT(setFontSize(int)));
	setObjectName("SolarSystem");
	connect(this, SIGNAL(flagOrbitsChanged(bool)),            this, SLOT(reconfigureOrbits()));
	connect(this, SIGNAL(flagPlanetsOrbitsChanged(bool)),     this, SLOT(reconfigureOrbits()));
	connect(this, SIGNAL(flagPlanetsOrbitsOnlyChanged(bool)), this, SLOT(reconfigureOrbits()));
	connect(this, SIGNAL(flagIsolatedOrbitsChanged(bool)),    this, SLOT(reconfigureOrbits()));
	connect(this, SIGNAL(flagOrbitsWithMoonsChanged(bool)),   this, SLOT(reconfigureOrbits()));

	markerArray=new MarkerVertex[maxMarkers*6];
	textureCoordArray = new unsigned char[maxMarkers*6*2];
	for (unsigned int i=0;i<maxMarkers; ++i)
	{
		static const unsigned char texElems[] = {0, 0, 255, 0, 255, 255, 0, 0, 255, 255, 0, 255};
		unsigned char* elem = &textureCoordArray[i*6*2];
		std::memcpy(elem, texElems, 12);
	}
}

void SolarSystem::setFontSize(int newFontSize)
{
	planetNameFont.setPixelSize(newFontSize);
}

SolarSystem::~SolarSystem()
{
	// release selected:
	selected.clear();
	selectedSSO.clear();
	for (auto* orb : std::as_const(orbits))
	{
		delete orb;
		orb = Q_NULLPTR;
	}
	sun.clear();
	moon.clear();
	earth.clear();
	Planet::hintCircleTex.clear();
	Planet::texEarthShadow.clear();

	delete[] markerArray;
	markerArray = nullptr;
	delete[] textureCoordArray;
	textureCoordArray = nullptr;
	delete markerShaderProgram;
	markerShaderProgram = nullptr;


	texEphemerisMarker.clear();
	texEphemerisCometMarker.clear();
	texEphemerisNowMarker.clear();
	texPointer.clear();

	delete allTrails;
	allTrails = Q_NULLPTR;

	// Get rid of circular reference between the shared pointers which prevent proper destruction of the Planet objects.
	for (const auto& p : std::as_const(systemPlanets))
	{
		p->satellites.clear();
	}

	//delete comet textures created in loadPlanets
	Comet::comaTexture.clear();
	Comet::tailTexture.clear();

	//deinit of SolarSystem is NOT called at app end automatically
	SolarSystem::deinit();
}

/*************************************************************************
 Re-implementation of the getCallOrder method
*************************************************************************/
double SolarSystem::getCallOrder(StelModuleActionName actionName) const
{
	if (actionName==StelModule::ActionDraw)
		return StelApp::getInstance().getModuleMgr().getModule("StarMgr")->getCallOrder(actionName)+10;
	return 0;
}

// Init and load the solar system data
void SolarSystem::init()
{
	initializeOpenGLFunctions();

	Q_ASSERT(conf);
	StelApp *app = &StelApp::getInstance();
	StelCore *core=app->getCore();

	Planet::init();
	loadPlanets();	// Load planets data

	// Compute position and matrix of sun and all the satellites (ie planets)
	// for the first initialization Q_ASSERT that center is sun center (only impacts on light speed correction)
	setExtraThreads(conf->value("astro/solar_system_threads", 0).toInt());
	computePositions(core, core->getJDE(), getSun());

	setSelected("");	// Fix a bug on macosX! Thanks Fumio!
	setFlagDrawMoonHalo(conf->value("viewing/flag_draw_moon_halo", true).toBool());
	setFlagDrawSunHalo(conf->value("viewing/flag_draw_sun_halo", true).toBool());
	setFlagMoonScale(conf->value("viewing/flag_moon_scaled", conf->value("viewing/flag_init_moon_scaled", false).toBool()).toBool());  // name change
	setMoonScale(conf->value("viewing/moon_scale", 4.0).toDouble());
	setMinorBodyScale(conf->value("viewing/minorbodies_scale", 10.0).toDouble());
	setFlagMinorBodyScale(conf->value("viewing/flag_minorbodies_scaled", false).toBool());
	setFlagPlanetScale(conf->value("viewing/flag_planets_scaled", false).toBool());
	setPlanetScale(conf->value("viewing/planets_scale", 150.0).toDouble());
	setFlagSunScale(conf->value("viewing/flag_sun_scaled", false).toBool());
	setSunScale(conf->value("viewing/sun_scale", 4.0).toDouble());
	setFlagPlanets(conf->value("astro/flag_planets").toBool());
	setFlagHints(conf->value("astro/flag_planets_hints").toBool());
	setFlagMarkers(conf->value("astro/flag_planets_markers", false).toBool());
	setMarkerMagThreshold(conf->value("astro/planet_markers_mag_threshold", 15.).toDouble());
	setFlagLabels(conf->value("astro/flag_planets_labels", true).toBool());
	setLabelsAmount(conf->value("astro/labels_amount", 3.).toDouble());
	setFlagOrbits(conf->value("astro/flag_planets_orbits").toBool());
	setFlagLightTravelTime(conf->value("astro/flag_light_travel_time", true).toBool());
	setFlagUseObjModels(conf->value("astro/flag_use_obj_models", false).toBool());
	setFlagShowObjSelfShadows(conf->value("astro/flag_show_obj_self_shadows", true).toBool());
	setFlagPointer(conf->value("astro/flag_planets_pointers", true).toBool());
	// Set the algorithm from Astronomical Almanac for computation of apparent magnitudes for
	// planets in case  observer on the Earth by default
	setApparentMagnitudeAlgorithmOnEarth(conf->value("astro/apparent_magnitude_algorithm", "Mallama2018").toString());
	// Is enabled the showing of isolated trails for selected objects only?
	setFlagIsolatedTrails(conf->value("viewing/flag_isolated_trails", true).toBool());
	setNumberIsolatedTrails(conf->value("viewing/number_isolated_trails", 1).toInt());
	setMaxTrailPoints(conf->value("viewing/max_trail_points", 5000).toInt());
	setMaxTrailTimeExtent(conf->value("viewing/max_trail_time_extent", 1).toInt());
	setFlagIsolatedOrbits(conf->value("viewing/flag_isolated_orbits", true).toBool());
	setFlagPlanetsOrbits(conf->value("viewing/flag_planets_orbits", false).toBool());
	setFlagPlanetsOrbitsOnly(conf->value("viewing/flag_planets_orbits_only", false).toBool());
	setFlagOrbitsWithMoons(conf->value("viewing/flag_orbits_with_moons", false).toBool());
	setFlagPermanentOrbits(conf->value("astro/flag_permanent_orbits", false).toBool());
	setOrbitColorStyle(conf->value("astro/planets_orbits_color_style", "one_color").toString());

	// Settings for calculation of position of Great Red Spot on Jupiter
	setGrsLongitude(conf->value("astro/grs_longitude", 46).toInt());
	setGrsDrift(conf->value("astro/grs_drift", 15.).toDouble());
	setGrsJD(conf->value("astro/grs_jd", 2460218.5).toDouble());

	setFlagEarthShadowEnlargementDanjon(conf->value("astro/shadow_enlargement_danjon", false).toBool());
	setFlagPermanentSolarCorona(conf->value("viewing/flag_draw_sun_corona", true).toBool());

	// Load colors from config file
	QString defaultColor = conf->value("color/default_color").toString();
	setLabelsColor(                    Vec3f(conf->value("color/planet_names_color", defaultColor).toString()));
	setOrbitsColor(                    Vec3f(conf->value("color/sso_orbits_color", defaultColor).toString()));
	setMajorPlanetsOrbitsColor(        Vec3f(conf->value("color/major_planet_orbits_color", "0.7,0.2,0.2").toString()));
	setMoonsOrbitsColor(               Vec3f(conf->value("color/moon_orbits_color", "0.7,0.2,0.2").toString()));
	setMinorPlanetsOrbitsColor(        Vec3f(conf->value("color/minor_planet_orbits_color", "0.7,0.5,0.5").toString()));
	setDwarfPlanetsOrbitsColor(        Vec3f(conf->value("color/dwarf_planet_orbits_color", "0.7,0.5,0.5").toString()));
	setCubewanosOrbitsColor(           Vec3f(conf->value("color/cubewano_orbits_color", "0.7,0.5,0.5").toString()));
	setPlutinosOrbitsColor(            Vec3f(conf->value("color/plutino_orbits_color", "0.7,0.5,0.5").toString()));
	setScatteredDiskObjectsOrbitsColor(Vec3f(conf->value("color/sdo_orbits_color", "0.7,0.5,0.5").toString()));
	setOortCloudObjectsOrbitsColor(    Vec3f(conf->value("color/oco_orbits_color", "0.7,0.5,0.5").toString()));
	setCometsOrbitsColor(              Vec3f(conf->value("color/comet_orbits_color", "0.7,0.8,0.8").toString()));
	setSednoidsOrbitsColor(            Vec3f(conf->value("color/sednoid_orbits_color", "0.7,0.5,0.5").toString()));
	setInterstellarOrbitsColor(        Vec3f(conf->value("color/interstellar_orbits_color", "1.0,0.6,1.0").toString()));
	setMercuryOrbitColor(              Vec3f(conf->value("color/mercury_orbit_color", "0.5,0.5,0.5").toString()));
	setVenusOrbitColor(                Vec3f(conf->value("color/venus_orbit_color", "0.9,0.9,0.7").toString()));
	setEarthOrbitColor(                Vec3f(conf->value("color/earth_orbit_color", "0.0,0.0,1.0").toString()));
	setMarsOrbitColor(                 Vec3f(conf->value("color/mars_orbit_color", "0.8,0.4,0.1").toString()));
	setJupiterOrbitColor(              Vec3f(conf->value("color/jupiter_orbit_color", "1.0,0.6,0.0").toString()));
	setSaturnOrbitColor(               Vec3f(conf->value("color/saturn_orbit_color", "1.0,0.8,0.0").toString()));
	setUranusOrbitColor(               Vec3f(conf->value("color/uranus_orbit_color", "0.0,0.7,1.0").toString()));
	setNeptuneOrbitColor(              Vec3f(conf->value("color/neptune_orbit_color", "0.0,0.3,1.0").toString()));
	setTrailsColor(                    Vec3f(conf->value("color/object_trails_color", defaultColor).toString()));
	setPointerColor(                   Vec3f(conf->value("color/planet_pointers_color", "1.0,0.3,0.3").toString()));

	// Ephemeris stuff
	setFlagEphemerisMarkers(conf->value("astrocalc/flag_ephemeris_markers", true).toBool());
	setFlagEphemerisAlwaysOn(conf->value("astrocalc/flag_ephemeris_alwayson", true).toBool());
	setFlagEphemerisNow(conf->value("astrocalc/flag_ephemeris_now", false).toBool());
	setFlagEphemerisDates(conf->value("astrocalc/flag_ephemeris_dates", false).toBool());
	setFlagEphemerisMagnitudes(conf->value("astrocalc/flag_ephemeris_magnitudes", false).toBool());
	setFlagEphemerisHorizontalCoordinates(conf->value("astrocalc/flag_ephemeris_horizontal", false).toBool());
	setFlagEphemerisLine(conf->value("astrocalc/flag_ephemeris_line", false).toBool());
	setEphemerisLineThickness(conf->value("astrocalc/ephemeris_line_thickness", 1).toInt());
	setFlagEphemerisSkipData(conf->value("astrocalc/flag_ephemeris_skip_data", false).toBool());
	setFlagEphemerisSkipMarkers(conf->value("astrocalc/flag_ephemeris_skip_markers", false).toBool());
	setEphemerisDataStep(conf->value("astrocalc/ephemeris_data_step", 1).toInt());	
	setFlagEphemerisSmartDates(conf->value("astrocalc/flag_ephemeris_smart_dates", true).toBool());
	setFlagEphemerisScaleMarkers(conf->value("astrocalc/flag_ephemeris_scale_markers", false).toBool());
	setEphemerisGenericMarkerColor( Vec3f(conf->value("color/ephemeris_generic_marker_color", "1.0,1.0,0.0").toString()));
	setEphemerisSecondaryMarkerColor( Vec3f(conf->value("color/ephemeris_secondary_marker_color", "0.7,0.7,1.0").toString()));
	setEphemerisSelectedMarkerColor(Vec3f(conf->value("color/ephemeris_selected_marker_color", "1.0,0.7,0.0").toString()));
	setEphemerisMercuryMarkerColor( Vec3f(conf->value("color/ephemeris_mercury_marker_color", "1.0,1.0,0.0").toString()));
	setEphemerisVenusMarkerColor(   Vec3f(conf->value("color/ephemeris_venus_marker_color", "1.0,1.0,1.0").toString()));
	setEphemerisMarsMarkerColor(    Vec3f(conf->value("color/ephemeris_mars_marker_color", "1.0,0.0,0.0").toString()));
	setEphemerisJupiterMarkerColor( Vec3f(conf->value("color/ephemeris_jupiter_marker_color", "0.3,1.0,1.0").toString()));
	setEphemerisSaturnMarkerColor(  Vec3f(conf->value("color/ephemeris_saturn_marker_color", "0.0,1.0,0.0").toString()));

	setOrbitsThickness(conf->value("astro/object_orbits_thickness", 1).toInt());
	setTrailsThickness(conf->value("astro/object_trails_thickness", 1).toInt());
	recreateTrails();
	setFlagTrails(conf->value("astro/flag_object_trails", false).toBool());

	StelObjectMgr *objectManager = GETSTELMODULE(StelObjectMgr);
	objectManager->registerStelObjectMgr(this);
	connect(objectManager, SIGNAL(selectedObjectChanged(StelModule::StelModuleSelectAction)),
		this, SLOT(selectedObjectChange(StelModule::StelModuleSelectAction)));

	texPointer = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/pointeur4.png");
	texEphemerisMarker = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/disk.png");
	texEphemerisNowMarker = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/gear.png");
	texEphemerisCometMarker = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/cometIcon.png");
	Planet::hintCircleTex = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/planet-indicator.png");
	markerCircleTex = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/planet-marker.png");

	connect(app, SIGNAL(languageChanged()), this, SLOT(updateI18n()));
	connect(&app->getSkyCultureMgr(), &StelSkyCultureMgr::currentSkyCultureChanged, this, &SolarSystem::updateSkyCulture);
	connect(&StelMainView::getInstance(), SIGNAL(reloadShadersRequested()), this, SLOT(reloadShaders()));
	connect(core, SIGNAL(locationChanged(StelLocation)), this, SLOT(recreateTrails()));
	connect(core, SIGNAL(dateChangedForTrails()), this, SLOT(recreateTrails()));

	QString displayGroup = N_("Display Options");
	addAction("actionShow_Planets", displayGroup, N_("Planets"), "planetsDisplayed", "P");
	addAction("actionShow_Planets_Labels", displayGroup, N_("Planet labels"), "labelsDisplayed", "Alt+P");
	addAction("actionShow_Planets_Orbits", displayGroup, N_("Planet orbits"), "flagOrbits", "O");
	addAction("actionShow_Planets_Trails", displayGroup, N_("Planet trails"), "trailsDisplayed", "Shift+T");
	addAction("actionShow_Planets_Trails_Reset", displayGroup, N_("Planet trails reset"), "recreateTrails()"); // No hotkey predefined.
	//there is a small discrepancy in the GUI: "Show planet markers" actually means show planet hints
	addAction("actionShow_Planets_Hints", displayGroup, N_("Planet markers"), "flagHints", "Ctrl+P");
	addAction("actionShow_Planets_Pointers", displayGroup, N_("Planet selection marker"), "flagPointer", "Ctrl+Shift+P");
	addAction("actionShow_Planets_EnlargeMoon", displayGroup, N_("Enlarge Moon"), "flagMoonScale");
	addAction("actionShow_Planets_EnlargeMinor", displayGroup, N_("Enlarge minor bodies"), "flagMinorBodyScale");
	addAction("actionShow_Planets_EnlargePlanets", displayGroup, N_("Enlarge Planets"), "flagPlanetScale");
	addAction("actionShow_Planets_EnlargeSun", displayGroup, N_("Enlarge Sun"), "flagSunScale");
	addAction("actionShow_Planets_ShowMinorBodyMarkers", displayGroup, N_("Mark minor bodies"), "flagMarkers");

	connect(StelApp::getInstance().getModule("HipsMgr"), SIGNAL(gotNewSurvey(HipsSurveyP)),
			this, SLOT(onNewSurvey(HipsSurveyP)));

	// Fill ephemeris dates
	connect(this, SIGNAL(requestEphemerisVisualization()), this, SLOT(fillEphemerisDates()));
	connect(this, SIGNAL(ephemerisDataStepChanged(int)), this, SLOT(fillEphemerisDates()));
	connect(this, SIGNAL(ephemerisSkipDataChanged(bool)), this, SLOT(fillEphemerisDates()));
	connect(this, SIGNAL(ephemerisSkipMarkersChanged(bool)), this, SLOT(fillEphemerisDates()));
	connect(this, SIGNAL(ephemerisSmartDatesChanged(bool)), this, SLOT(fillEphemerisDates()));


	// Create shader program for mass drawing of asteroid markers
	QOpenGLShader vshader(QOpenGLShader::Vertex);
	const char *vsrc =
		"ATTRIBUTE mediump vec2 pos;\n"
		"ATTRIBUTE mediump vec2 texCoord;\n"
		"ATTRIBUTE mediump vec3 color;\n"
		"uniform mediump mat4 projectionMatrix;\n"
		"VARYING mediump vec2 texc;\n"
		"VARYING mediump vec3 outColor;\n"
		"void main(void)\n"
		"{\n"
		"    gl_Position = projectionMatrix * vec4(pos.x, pos.y, 0, 1);\n"
		"    texc = texCoord;\n"
		"    outColor = color;\n"
		"}\n";
	vshader.compileSourceCode(StelOpenGL::globalShaderPrefix(StelOpenGL::VERTEX_SHADER) + vsrc);
	if (!vshader.log().isEmpty()) { qWarning() << "SolarSystem::init(): Warnings while compiling vshader: " << vshader.log(); }

	QOpenGLShader fshader(QOpenGLShader::Fragment);
	const char *fsrc =
		"VARYING mediump vec2 texc;\n"
		"VARYING mediump vec3 outColor;\n"
		"uniform sampler2D tex;\n"
		"void main(void)\n"
		"{\n"
		"    FRAG_COLOR = texture2D(tex, texc)*vec4(outColor, 1.);\n"
		"}\n";
	fshader.compileSourceCode(StelOpenGL::globalShaderPrefix(StelOpenGL::FRAGMENT_SHADER) + fsrc);
	if (!fshader.log().isEmpty()) { qWarning() << "SolarSystem::init(): Warnings while compiling fshader: " << fshader.log(); }

	markerShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	markerShaderProgram->addShader(&vshader);
	markerShaderProgram->addShader(&fshader);
	StelPainter::linkProg(markerShaderProgram, "starShader");
	markerShaderVars.projectionMatrix = markerShaderProgram->uniformLocation("projectionMatrix");
	markerShaderVars.texCoord = markerShaderProgram->attributeLocation("texCoord");
	markerShaderVars.pos = markerShaderProgram->attributeLocation("pos");
	markerShaderVars.color = markerShaderProgram->attributeLocation("color");
	markerShaderVars.texture = markerShaderProgram->uniformLocation("tex");

	vbo->create();
	vbo->bind();
	vbo->setUsagePattern(QOpenGLBuffer::StreamDraw);
	vbo->allocate(maxMarkers*6*sizeof(MarkerVertex) + maxMarkers*6*2);

	if(vao->create())
	{
		vao->bind();
		setupCurrentVAO();
		vao->release();
	}

	vbo->release();
}

void SolarSystem::setupCurrentVAO()
{
	vbo->bind();
	markerShaderProgram->setAttributeBuffer(markerShaderVars.pos, GL_FLOAT, 0, 2, sizeof(MarkerVertex));
	markerShaderProgram->setAttributeBuffer(markerShaderVars.color, GL_UNSIGNED_BYTE, offsetof(MarkerVertex,color), 3, sizeof(MarkerVertex));
	markerShaderProgram->setAttributeBuffer(markerShaderVars.texCoord, GL_UNSIGNED_BYTE, maxMarkers*6*sizeof(MarkerVertex), 2, 0);
	vbo->release();
	markerShaderProgram->enableAttributeArray(markerShaderVars.pos);
	markerShaderProgram->enableAttributeArray(markerShaderVars.color);
	markerShaderProgram->enableAttributeArray(markerShaderVars.texCoord);
}

void SolarSystem::bindVAO()
{
	if(vao->isCreated())
		vao->bind();
	else
		setupCurrentVAO();
}

void SolarSystem::releaseVAO()
{
	if(vao->isCreated())
	{
		vao->release();
	}
	else
	{
		markerShaderProgram->disableAttributeArray(markerShaderVars.pos);
		markerShaderProgram->disableAttributeArray(markerShaderVars.color);
		markerShaderProgram->disableAttributeArray(markerShaderVars.texCoord);
	}
}

void SolarSystem::deinit()
{
	Planet::deinitShader();
	Planet::deinitFBO();
}

void SolarSystem::resetTextures(const QString &planetName)
{
	if (planetName.isEmpty())
	{
		for (const auto& p : std::as_const(systemPlanets))
		{
			p->resetTextures();
		}
	}
	else
	{
		PlanetP planet = searchByEnglishName(planetName);
		if (!planet.isNull())
			planet->resetTextures();
	}
}

void SolarSystem::setTextureForPlanet(const QString& planetName, const QString& texName)
{
	PlanetP planet = searchByEnglishName(planetName);
	if (!planet.isNull())
		planet->replaceTexture(texName);
	else
		qWarning() << "The planet" << planetName << "was not found. Please check the name.";
}

void SolarSystem::recreateTrails()
{
	// Create a trail group containing all the planets orbiting the sun (not including satellites)
	if (allTrails!=Q_NULLPTR)
		delete allTrails;
	allTrails = new TrailGroup(maxTrailTimeExtent * 365.f, maxTrailPoints);

	unsigned long cnt = static_cast<unsigned long>(selectedSSO.size());
	if (cnt>0 && getFlagIsolatedTrails())
	{
		unsigned long limit = static_cast<unsigned long>(getNumberIsolatedTrails());
		if (cnt<limit)
			limit = cnt;
		for (unsigned long i=0; i<limit; i++)
		{
			if (selectedSSO[cnt - i - 1]->getPlanetType() != Planet::isObserver)
				allTrails->addObject(static_cast<QSharedPointer<StelObject>>(selectedSSO[cnt - i - 1]), &trailsColor);
		}
	}
	else
	{
		for (const auto& p : std::as_const(getSun()->satellites))
		{
			if (p->getPlanetType() != Planet::isObserver)
				allTrails->addObject(static_cast<QSharedPointer<StelObject>>(p), &trailsColor);
		}
		// Add moons of current planet
		StelCore *core=StelApp::getInstance().getCore();
		const StelObserver *obs=core->getCurrentObserver();
		if (obs)
		{
			const QSharedPointer<Planet> planet=obs->getHomePlanet();
			for (const auto& m : std::as_const(planet->satellites))
				if (m->getPlanetType() != Planet::isObserver)
					allTrails->addObject(static_cast<QSharedPointer<StelObject>>(m), &trailsColor);
		}
	}
}


void SolarSystem::updateSkyCulture(const StelSkyCulture& skyCulture)
{
	for (const auto& p : std::as_const(systemPlanets))
		p->removeAllCulturalNames();

	if (!skyCulture.names.isEmpty())
		loadCultureSpecificNames(skyCulture.names);

	updateI18n();
}

void SolarSystem::loadCultureSpecificNames(const QJsonObject& data)
{
	const StelTranslator& trans = StelApp::getInstance().getLocaleMgr().getSkyTranslator();

	for (auto it = data.begin(); it != data.end(); ++it)
	{
		const auto key = it.key();

		if (key.startsWith("NAME "))
		{
			const QString planetId = key.mid(5);
			const QJsonArray names = it.value().toArray(); // The array of name dicts

			PlanetP planet = searchByEnglishName(planetId);
			if (!planet)
				continue;

			for (const auto& nameVal : names)
			{
				const QJsonObject json = nameVal.toObject();

				StelObject::CulturalName cName;
				cName.translated=json["english"].toString();
				cName.native=json["native"].toString();
				//if (cName.native.isEmpty()) cName.native=cName.translated;
				cName.pronounce=json["pronounce"].toString();
				if (cName.native.isEmpty())
				{
					if (cName.pronounce.isEmpty())
						cName.native=cName.pronounce=cName.translated;
					else
						cName.native=cName.pronounce;
				}

				cName.translatedI18n=trans.qtranslate(cName.translated, json["context"].toString());
				cName.pronounceI18n=trans.qtranslate(cName.pronounce, json["context"].toString());
				cName.transliteration=json["transliteration"].toString();
				cName.IPA=json["IPA"].toString();

				if (json.contains("visible"))
				{
					QString visible=json["visible"].toString();
					if (visible==L1S("morning"))
						cName.special=StelObject::CulturalNameSpecial::Morning;
					else if (visible==L1S("evening"))
						cName.special=StelObject::CulturalNameSpecial::Evening;
					else
						qWarning() << "Bad value for \"visible\". Ignoring.";
				}

				planet->addCulturalName(cName);
			}
		}
	}
}

void SolarSystem::reloadShaders()
{
	Planet::deinitShader();
	Planet::initShader();
}

void SolarSystem::drawPointer(const StelCore* core)
{
	const StelProjectorP prj = core->getProjection(StelCore::FrameJ2000);
	static StelObjectMgr *sObjMgr=GETSTELMODULE(StelObjectMgr);

	const QList<StelObjectP> newSelected = sObjMgr->getSelectedObject("Planet");
	if (!newSelected.empty())
	{
		const StelObjectP obj = newSelected[0];
		Vec3d pos=obj->getJ2000EquatorialPos(core);

		Vec3f screenpos;
		// Compute 2D pos and return if outside screen
		if (!prj->project(pos, screenpos))
			return;

		StelPainter sPainter(prj);
		sPainter.setColor(getPointerColor());

		float screenSize = static_cast<float>(obj->getAngularRadius(core))*prj->getPixelPerRadAtCenter()*M_PI_180f*2.f;
		
		const float scale = static_cast<float>(prj->getDevicePixelsPerPixel());
		screenSize+= scale * (45.f + 10.f*std::sin(2.f * static_cast<float>(StelApp::getInstance().getAnimationTime())));

		texPointer->bind();

		sPainter.setBlending(true);

		screenSize*=0.5f;
		const float angleBase = static_cast<float>(StelApp::getInstance().getAnimationTime()) * 10;
		// We draw 4 instances of the sprite at the corners of the pointer
		for (int i = 0; i < 4; ++i)
		{
			const float angle = angleBase + i * 90;
			const float x = screenpos[0] + screenSize * cos(angle * M_PI_180f);
			const float y = screenpos[1] + screenSize * sin(angle * M_PI_180f);
			sPainter.drawSprite2dMode(x, y, 10, angle);
		}
	}
}

void keplerOrbitPosFunc(double jd,double xyz[3], double xyzdot[3], void* orbitPtr)
{
	static_cast<KeplerOrbit*>(orbitPtr)->positionAtTimevInVSOP87Coordinates(jd, xyz);
	static_cast<KeplerOrbit*>(orbitPtr)->getVelocity(xyzdot);
}

void gimbalOrbitPosFunc(double jd,double xyz[3], double xyzdot[3], void* orbitPtr)
{
	static_cast<GimbalOrbit*>(orbitPtr)->positionAtTimevInVSOP87Coordinates(jd, xyz);
	static_cast<GimbalOrbit*>(orbitPtr)->getVelocity(xyzdot);
}

// Init and load the solar system data (2 files)
void SolarSystem::loadPlanets()
{
	minorBodies.clear();
	systemMinorBodies.clear();
	qInfo() << "Loading Solar System data (1: planets and moons) ...";
	QString solarSystemFile = StelFileMgr::findFile("data/ssystem_major.ini");
	if (solarSystemFile.isEmpty())
	{
		qWarning() << "ERROR while loading ssystem_major.ini (unable to find data/ssystem_major.ini):" << StelUtils::getEndLineChar();
		return;
	}

	if (!loadPlanets(solarSystemFile))
	{
		qWarning() << "ERROR while loading ssystem_major.ini:" << StelUtils::getEndLineChar();
		return;
	}

	qInfo() << "Loading Solar System data (2: minor bodies) ...";
	QStringList solarSystemFiles = StelFileMgr::findFileInAllPaths("data/ssystem_minor.ini");
	if (solarSystemFiles.isEmpty())
	{
		qWarning() << "ERROR while loading ssystem_minor.ini (unable to find data/ssystem_minor.ini):" << StelUtils::getEndLineChar();
		return;
	}

	for (const auto& solarSystemFile : std::as_const(solarSystemFiles))
	{
		if (loadPlanets(solarSystemFile))
		{
			qInfo().noquote() << "File ssystem_minor.ini is loaded successfully...";
			break;
		}
		else
		{
//			sun.clear();
//			moon.clear();
//			earth.clear();
			//qCritical() << "We should not be here!";

			qDebug() << "Removing minor bodies";
			for (const auto& p : std::as_const(systemPlanets))
			{
				// We can only delete minor objects now!
				if (p->pType >= Planet::isAsteroid)
				{
					p->satellites.clear();
				}
			}			
			systemPlanets.clear();			
			//Memory leak? What's the proper way of cleaning shared pointers?

			// TODO: 0.16pre what about the orbits list?

			//If the file is in the user data directory, rename it:
			if (solarSystemFile.contains(StelFileMgr::getUserDir()))
			{
				QString newName = QString("%1/data/ssystem_minor-%2.ini").arg(StelFileMgr::getUserDir(), QDateTime::currentDateTime().toString("yyyyMMddThhmmss"));
				if (QFile::rename(solarSystemFile, newName))
					qWarning() << "Invalid Solar System file" << QDir::toNativeSeparators(solarSystemFile) << "has been renamed to" << QDir::toNativeSeparators(newName);
				else
				{
					qWarning() << "Invalid Solar System file" << QDir::toNativeSeparators(solarSystemFile) << "cannot be removed!";
					qWarning() << "Please either delete it, rename it or move it elsewhere.";
				}
			}
		}
	}

	shadowPlanetCount = 0;

	for (const auto& planet : std::as_const(systemPlanets))
		if(planet->parent != sun || !planet->satellites.isEmpty())
			shadowPlanetCount++;
}

unsigned char SolarSystem::BvToColorIndex(double bV)
{
	const double dBV = qBound(-500., static_cast<double>(bV)*1000.0, 3499.);
	return static_cast<unsigned char>(std::floor(0.5+127.0*((500.0+dBV)/4000.0)));
}

bool SolarSystem::loadPlanets(const QString& filePath)
{
	StelSkyDrawer* skyDrawer = StelApp::getInstance().getCore()->getSkyDrawer();
	qInfo().noquote() << "Loading from:"  << filePath;
	QSettings pd(filePath, StelIniFormat);
	if (pd.status() != QSettings::NoError)
	{
		qWarning().noquote() << "ERROR while parsing" << QDir::toNativeSeparators(filePath);
		return false;
	}

	// QSettings does not allow us to say that the sections of the file
	// will be listed in the same order  as in the file like the old
	// InitParser used to so we can no longer assume that.
	//
	// This means we must first decide what order to read the sections
	// of the file in (each section contains one planet/moon/asteroid/comet/...) to avoid setting
	// the parent Planet* to one which has not yet been created.
	//
	// Stage 1: Make a map of body names back to the section names
	// which they come from. Also make a map of body name to parent body
	// name. These two maps can be made in a single pass through the
	// sections of the file.
	//
	// Stage 2: Make an ordered list of section names such that each
	// item is only ever dependent on items which appear earlier in the
	// list.
	// 2a: Make a QMultiMap relating the number of levels of dependency
	//     to the body name, i.e.
	//     0 -> Sun
	//     1 -> Mercury
	//     1 -> Venus
	//     1 -> Earth
	//     2 -> Moon
	//     etc.
	// 2b: Populate an ordered list of section names by iterating over
	//     the QMultiMap.  This type of container is always sorted on the
	//     key in ascending order, so it's easy.
	//     i.e. [sun, earth, moon] is fine, but not [sun, moon, earth]
	//
	// Stage 3: iterate over the ordered sections decided in stage 2,
	// creating the planet objects from the QSettings data.

	// Stage 1 (as described above).
	QMap<QString, QString> secNameMap;
	QMap<QString, QString> parentMap;
	QStringList sections = pd.childGroups();
	// qDebug() << "Stage 1: load ini file with" << sections.size() << "entries: "<< sections;
	for (int i=0; i<sections.size(); ++i)
	{
		const QString secname = sections.at(i);
		const QString englishName = pd.value(secname+"/name", pd.value(secname+"/iau_designation")).toString();
		if (englishName.isEmpty())
			qWarning().noquote() << "SSO without proper name found in" << QDir::toNativeSeparators(filePath) << "section" << secname;
		const QString strParent = pd.value(secname+"/parent", "Sun").toString();
		// Only for sorting here we must find our own temporary object name. This is similar but not equal to IAU practice, and must be exactly repeated in the next loop in Stage 2a.
		QString obName=englishName;
		const bool isMinor=QStringList({"asteroid", "plutino", "comet", "dwarf planet", "cubewano", "scattered disc object", "oco", "sednoid", "interstellar object"}).contains(pd.value(secname+"/type").toString());
		if (isMinor && englishName!=L1S("Pluto"))
		{
			const QString designation = pd.value(secname+"/iau_designation", pd.value(secname+"/minor_planet_number")).toString();
			if (designation.isEmpty() && pd.value(secname+"/type") != L1S("comet"))
				qWarning().noquote() << "Minor body " << englishName << "has incomplete data (missing iau_designation or minor_planet_number) in" << QDir::toNativeSeparators(filePath) << "section" << secname;
			obName=(QString("%1 (%2)").arg(designation, englishName));
		}
		if (secNameMap.contains(obName))
			qWarning() << "secNameMap already contains " << obName << ". Overwriting data.";
		secNameMap[obName] = secname;
		if (strParent!=L1S("none") && !strParent.isEmpty() && !englishName.isEmpty())
		{
			parentMap[obName] = strParent;
			// qDebug() << "parentmap[" << obName << "] = " << strParent;
		}
	}

	// Stage 2a (as described above).
	QMultiMap<int, QString> depLevelMap;
	for (int i=0; i<sections.size(); ++i)
	{
		const QString secname = sections.at(i);
		const QString englishName = pd.value(secname+"/name", pd.value(secname+"/iau_designation")).toString();
		QString obName=englishName;
		const bool isMinor=QStringList({"asteroid", "plutino", "comet", "dwarf planet", "cubewano", "scattered disc object", "oco", "sednoid", "interstellar object"}).contains(pd.value(secname+"/type").toString());
		if (isMinor && englishName!=L1S("Pluto"))
		{
			const QString designation = pd.value(secname+"/iau_designation", pd.value(secname+"/minor_planet_number")).toString();
			obName=(QString("%1 (%2)").arg(designation, englishName));
		}
		// follow dependencies, incrementing level when we have one till we run out.
		QString p=obName;
		int level = 0;
		while(parentMap.contains(p) && parentMap[p]!=L1S("none"))
		{
			level++;
			p = parentMap[p];
		}

		depLevelMap.insert(level, secNameMap[obName]);
		// qDebug() << "2a: Level" << level << "secNameMap[" << obName << "]="<< secNameMap[obName];
	}

	// Stage 2b (as described above).
	// qDebug() << "Stage 2b:";
	QStringList orderedSections;
#if (QT_VERSION>=QT_VERSION_CHECK(6,0,0))
	QMultiMapIterator<int, QString> levelMapIt(depLevelMap);
#else
	QMapIterator<int, QString> levelMapIt(depLevelMap);
#endif
	while(levelMapIt.hasNext())
	{
		levelMapIt.next();
		orderedSections << levelMapIt.value();
	}
	// qDebug() << orderedSections;

	// Stage 3 (as described above).
	int readOk=0;
	//int totalPlanets=0;

	// qDebug() << "Adding " << orderedSections.size() << "objects...";
	for (int i = 0;i<orderedSections.size();++i)
	{
		// qDebug() << "Processing entry" << orderedSections.at(i);

		//totalPlanets++;
		const QString secname = orderedSections.at(i);
		const QString type = pd.value(secname+"/type").toString();
		QString englishName = pd.value(secname+"/name").toString().simplified();
		// englishName alone may be a combination of several elements...
		if (type==L1S("comet") || type == L1S("interstellar object"))
		{
			static const QRegularExpression periodicRe("^([1-9][0-9]*[PD](-\\w+)?)"); // No "/" at end, there are nameless numbered comets! (e.g. 362P, 396P)
			QRegularExpressionMatch periodMatch=periodicRe.match(englishName);

			static const QRegularExpression iauDesignationRe("^([1-9][0-9]*[PD](-\\w+)?)|([CA]/[-0-9]+\\s[A-Y])");
			QRegularExpressionMatch iauDesignationMatch=iauDesignationRe.match(englishName);

			// Our name rules for the final englishName, which must contain one element in brackets unless it starts with "A/".
			// Numbered periodic comets: "1P/Halley (1986)"
			// Reclassified Asteroid like "A/2022 B3"
			// All others: C-AX/2023 A2 (discoverer)". (with optional fragment code -AX)
			const QString iauDesignation = pd.value(secname+"/iau_designation").toString();
			const QString dateCode =      pd.value(secname+"/date_code").toString();
			const QString perihelCode =   pd.value(secname+"/perihelion_code").toString();
			const QString discoveryCode = pd.value(secname+"/discovery_code").toString();
			if (iauDesignation.isEmpty() && perihelCode.isEmpty() && discoveryCode.isEmpty() && !periodMatch.hasMatch() && !iauDesignationMatch.hasMatch())
			{
				qWarning() << "Comet " << englishName << "has no IAU designation, no perihelion code, no discovery code and seems not a numbered comet in section " << secname;
			}
			// order of codes: date_code [P/1982 U1] - perihelion_code [1986 III] - discovery_code [1982i]

			// The test here can be improved, e.g. with a regexp. In case the name is already reasonably complete, we do not re-build it from the available elements for now. However, the ini file should provide the elements separated!
			if (iauDesignation.isEmpty() && !englishName.contains("(") && (!discoveryCode.isEmpty()))
			{
					englishName.append(QString(" (%1)").arg(discoveryCode));
			}
			else if (iauDesignation.isEmpty() && !englishName.contains("("))
			{
				// Prepare perihel year to attach to the name when loading an old ssystem_minor.ini where this name component is missing
				QString periStr;
				double periJD=pd.value(secname+"/orbit_TimeAtPericenter").toDouble();
				if (periJD)
				{
					int day, month, year;
					StelUtils::getDateFromJulianDay(periJD, &year, &month, &day);
					periStr=QString::number(year);
				}
				if (!periStr.isEmpty())
					englishName.append(QString(" (%1)").arg(periStr));
				else
					qWarning() << "orbit_TimeAtPericenter may be invalid for object" << englishName << "in section" << secname;
			}
			else if (!iauDesignation.isEmpty() && !englishName.contains("(") && !englishName.contains("/") && !englishName.startsWith("A/"))
				englishName=QString("%1 (%2)").arg(iauDesignation, englishName); // recombine name and iau_designation if name is only the discoverer name.

			if (!englishName.contains("(") && !englishName.startsWith("A/"))
			{
				QString name;
				if (!iauDesignation.isEmpty())
				{
					name=iauDesignation;
					if (!englishName.isEmpty())
						name.append(QString(" (%1)").arg(englishName));
				}
				else if (!dateCode.isEmpty())
				{
					name=dateCode;
					if (!englishName.isEmpty())
						name.append(QString(" (%1)").arg(englishName));
				}
				else if (!discoveryCode.isEmpty())
				{
					name=discoveryCode;
					if (!englishName.isEmpty())
						name.append(QString(" (%1)").arg(englishName));
				}
				else if (!perihelCode.isEmpty())
				{
					name.append(QString("C/%1").arg(perihelCode)); // This is not classic, but a final fallback before warning
					if (!englishName.isEmpty())
						name.append(QString(" (%1)").arg(englishName));
				}

				if (name.contains("("))
					englishName=name;
				else
					qWarning() << "Comet " << englishName << "has no proper name elements in section " << secname;
			}
		}
		else if (QStringList({"asteroid", "dwarf planet", "cubewano", "sednoid", "plutino", "scattered disc object", "Oort cloud object"}).contains(type) && !englishName.contains("Pluto"))
		{
			const int minorPlanetNumber= pd.value(secname+"/minor_planet_number").toInt();
			const QString iauDesignation = pd.value(secname+"/iau_designation", "").toString();
			if (iauDesignation.isEmpty() && minorPlanetNumber==0)
				qWarning() << "minor body " << englishName << "has no IAU code in section " << secname;
			const QString discoveryCode = pd.value(secname+"/discovery_code").toString();

			// The test here can be improved, e.g. with a regexp. In case the name is already reasonably complete, we do not re-build it from the available elements
			if (englishName.isEmpty())
			{
				//if (minorPlanetNumber>0)
				//	englishName=QString("(%1) ").arg(minorPlanetNumber);
				if (!iauDesignation.isEmpty())
				{
					englishName.append(iauDesignation);
				}
				else if (!discoveryCode.isEmpty())
				{
					englishName.append(discoveryCode);
				}
				else
					qWarning() << "Minor body in section" << secname << "has no proper name elements";
			}
		}

		const double bV = pd.value(secname+"/color_index_bv", 99.).toDouble();
		const QString strParent = pd.value(secname+"/parent", "Sun").toString(); // Obvious default, keep file entries simple.
		PlanetP parent;
		if (strParent!=L1S("none"))
		{
			// Look in the other planets the one named with strParent
			for (const auto& p : std::as_const(systemPlanets))
			{
				if (p->getEnglishName()==strParent)
				{
					parent = p;
					break;
				}
			}
			if (parent.isNull())
			{
				qWarning().noquote().nospace() << "ERROR: can't find parent solar system body for " << englishName << ". Skipping.";
				//abort();
				continue;
			}
		}
		Q_ASSERT(parent || englishName==L1S("Sun"));

		const QString coordFuncName = pd.value(secname+"/coord_func", "kepler_orbit").toString(); // 0.20: new default for all non *_special.
		// qDebug() << "englishName:" << englishName << ", parent:" << strParent <<  ", coord_func:" << coordFuncName;
		posFuncType posfunc=Q_NULLPTR;
		Orbit* orbitPtr=Q_NULLPTR;
		OsculatingFunctType *osculatingFunc = Q_NULLPTR;
		bool closeOrbit = true;
		double semi_major_axis=0; // used again below.


#ifdef USE_GIMBAL_ORBIT
		// undefine the flag in Orbit.h to disable and use the old, static observer solution (on an infinitely slow KeplerOrbit)
		// Note that for now we ignore any orbit-related config values except orbit_SemiMajorAxis from the ini file.
		if (type==L1S("observer"))
		{
			double unit = 1; // AU
			double defaultSemiMajorAxis = 1;
			if (strParent!=L1S("Sun"))
			{
				unit /= AU;  // Planet moons have distances given in km in the .ini file! But all further computation done in AU.
				defaultSemiMajorAxis *= AU;
			}
			semi_major_axis = pd.value(secname+"/orbit_SemiMajorAxis", defaultSemiMajorAxis).toDouble() * unit;
			// Create a pseudo orbit that allows interaction with keyboard
			GimbalOrbit *orb = new GimbalOrbit(semi_major_axis, 0.*M_PI_180, 45.*M_PI_180); // [Over mid-north latitude]
			orb->setMinDistance(parent->getEquatorialRadius()*1.5);
			orbits.push_back(orb);

			orbitPtr = orb;
			posfunc = &gimbalOrbitPosFunc;
		}
		else
#endif
		if (coordFuncName==L1S("kepler_orbit") || coordFuncName==L1S("comet_orbit") || coordFuncName==L1S("ell_orbit")) // ell_orbit used for planet moons. TBD in V1.0: remove non-kepler_orbit!
		{
			if (coordFuncName!=L1S("kepler_orbit"))
				qDebug() << "Old-fashioned entry" << coordFuncName << "found. Please delete line from " << filePath << "section" << secname;
			// ell_orbit was used for planet moons, comet_orbit for minor bodies. The only difference is that pericenter distance for moons is given in km, not AU.
			// Read the orbital elements			
			const double eccentricity = pd.value(secname+"/orbit_Eccentricity", 0.0).toDouble();
			if (eccentricity >= 1.0) closeOrbit = false;
			double pericenterDistance = pd.value(secname+"/orbit_PericenterDistance",-1e100).toDouble(); // AU, or km for Moons (those where parent!=sun)!
			if (pericenterDistance <= 0.0) {
				semi_major_axis = pd.value(secname+"/orbit_SemiMajorAxis",-1e100).toDouble();
				if (semi_major_axis <= -1e100) {
					qDebug() << "ERROR loading " << englishName << "from section" << secname
						 << ": you must provide orbit_PericenterDistance or orbit_SemiMajorAxis. Skipping " << englishName;
					continue;
				} else {
					Q_ASSERT(eccentricity != 1.0); // parabolic orbits have no semi_major_axis
					pericenterDistance = semi_major_axis * (1.0-eccentricity);
				}
			} else {
				semi_major_axis = (eccentricity == 1.0)
								? 0.0 // parabolic orbits have no semi_major_axis
								: pericenterDistance / (1.0-eccentricity);
			}
			if (strParent!=L1S("Sun"))
				pericenterDistance /= AU;  // Planet moons have distances given in km in the .ini file! But all further computation done in AU.

			double meanMotion = pd.value(secname+"/orbit_MeanMotion",-1e100).toDouble(); // degrees/day
			if (meanMotion <= -1e100) {
				const double period = pd.value(secname+"/orbit_Period",-1e100).toDouble();
				if (period <= -1e100) {
					if (parent->getParent()) {
						qWarning().noquote() << "ERROR: " << englishName
							   << ": when the parent body is not the sun, you must provide "
							   << "either orbit_MeanMotion or orbit_Period";
					} else {
						// in case of parent=sun: use Gaussian gravitational constant for calculating meanMotion:
						meanMotion = (eccentricity == 1.0)
									? 0.01720209895 * (1.5/pericenterDistance) * std::sqrt(0.5/pericenterDistance)  // Heafner: Fund.Eph.Comp. W / dt
									: 0.01720209895 / (fabs(semi_major_axis)*std::sqrt(fabs(semi_major_axis)));
					}
				} else {
					meanMotion = 2.0*M_PI/period;
				}
			} else {
				meanMotion *= (M_PI/180.0);
			}

			const double ascending_node = pd.value(secname+"/orbit_AscendingNode", 0.0).toDouble()*(M_PI/180.0);
			double arg_of_pericenter = pd.value(secname+"/orbit_ArgOfPericenter",-1e100).toDouble();
			double long_of_pericenter;
			if (arg_of_pericenter <= -1e100) {
				long_of_pericenter = pd.value(secname+"/orbit_LongOfPericenter", 0.0).toDouble()*(M_PI/180.0);
				arg_of_pericenter = long_of_pericenter - ascending_node;
			} else {
				arg_of_pericenter *= (M_PI/180.0);
				long_of_pericenter = arg_of_pericenter + ascending_node;
			}

			double time_at_pericenter = pd.value(secname+"/orbit_TimeAtPericenter",-1e100).toDouble();
			// In earlier times (up to 0.21.2) we did not care much to store orbital epoch for comets but silently assumed T for it in various places.
			// However, the distinction is relevant to discern element sets for various valid ranges.
			// Comet orbits epoch should default to T while planets or moons default to J2000.
			const double epoch = pd.value(secname+"/orbit_Epoch", type==L1S("comet") ? time_at_pericenter : Planet::J2000).toDouble();
			if (time_at_pericenter <= -1e100) {
				double mean_anomaly = pd.value(secname+"/orbit_MeanAnomaly",-1e100).toDouble()*(M_PI/180.0);
				if (mean_anomaly <= -1e10) {
					double mean_longitude = pd.value(secname+"/orbit_MeanLongitude",-1e100).toDouble()*(M_PI/180.0);
					if (mean_longitude <= -1e10) {
						qWarning().noquote() << "ERROR: " << englishName
							   << ": when you do not provide orbit_TimeAtPericenter, you must provide orbit_Epoch"
							   << "and either one of orbit_MeanAnomaly or orbit_MeanLongitude. Skipping this object.";
						//abort();
						continue;
					} else {
						mean_anomaly = mean_longitude - long_of_pericenter;
					}
				}
				time_at_pericenter = epoch - mean_anomaly / meanMotion;
			}

			static const QMap<QString, double>massMap={ // masses from DE430/431
				{ "Sun",            1.0},
				{ "Mercury",  6023682.155592},
				{ "Venus",     408523.718658},
				{ "Earth",     332946.048834},
				{ "Mars",     3098703.590291},
				{ "Jupiter",     1047.348625},
				{ "Saturn",      3497.901768},
				{ "Uranus",     22902.981613},
				{ "Neptune",    19412.259776},
				{ "Pluto",  135836683.768617}};

			// Construct orbital elements relative to the parent body. This will construct orbits for J2000 only.
			// Some planet axes move very slowly, this effect could be modelled by replicating these lines
			// after recomputing obliquity and node (below) in Planet::computeTransMatrix().
			// The effect is negligible for several millennia, though.
			// When the parent is the sun use ecliptic rather than sun equator:
			const double parentRotObliquity  = parent->getParent() ? parent->getRotObliquity(Planet::J2000) : 0.0;
			const double parent_rot_asc_node = parent->getParent() ? parent->getRotAscendingNode()  : 0.0;
			double parent_rot_j2000_longitude = 0.0;
			if (parent->getParent()) {
				const double c_obl = cos(parentRotObliquity);
				const double s_obl = sin(parentRotObliquity);
				const double c_nod = cos(parent_rot_asc_node);
				const double s_nod = sin(parent_rot_asc_node);
				const Vec3d OrbitAxis0( c_nod,       s_nod,        0.0);
				const Vec3d OrbitAxis1(-s_nod*c_obl, c_nod*c_obl,s_obl);
				const Vec3d OrbitPole(  s_nod*s_obl,-c_nod*s_obl,c_obl);
				const Vec3d J2000Pole(StelCore::matJ2000ToVsop87.multiplyWithoutTranslation(Vec3d(0,0,1)));
				Vec3d J2000NodeOrigin(J2000Pole^OrbitPole);
				J2000NodeOrigin.normalize();
				parent_rot_j2000_longitude = atan2(J2000NodeOrigin*OrbitAxis1,J2000NodeOrigin*OrbitAxis0);
			}

			const double orbitGoodDays=pd.value(secname+"/orbit_good", parent->englishName!=L1S("Sun") ? 0. : -1.).toDouble(); // "Moons" have permanently good orbits.
			const double inclination = pd.value(secname+"/orbit_Inclination", 0.0).toDouble()*(M_PI/180.0);

			// Create a Keplerian orbit. This has been called CometOrbit before 0.20.
			//qDebug() << "Creating KeplerOrbit for" << parent->englishName << "---" << englishName;
			KeplerOrbit *orb = new KeplerOrbit(epoch,                  // JDE
							   pericenterDistance,     // [AU]
							   eccentricity,           // 0..>1 (>>1 for Interstellar objects)
							   inclination,            // [radians]
							   ascending_node,         // [radians]
							   arg_of_pericenter,      // [radians]
							   time_at_pericenter,     // JDE
							   orbitGoodDays,          // orbitGoodDays. 0=always good, -1=compute_half_orbit_duration
							   meanMotion,             // [radians/day]
							   parentRotObliquity,     // [radians]
							   parent_rot_asc_node,    // [radians]
							   parent_rot_j2000_longitude, // [radians]
							   1./massMap.value(parent->englishName, 1.)); // central mass [solar masses]
			orbits.push_back(orb);

			orbitPtr = orb;
			posfunc = &keplerOrbitPosFunc;
		}
		else
		{
			static const QMap<QString, posFuncType>posfuncMap={
				{ "sun_special",       &get_sun_barycentric_coordsv},
				{ "mercury_special",   &get_mercury_helio_coordsv},
				{ "venus_special",     &get_venus_helio_coordsv},
				{ "earth_special",     &get_earth_helio_coordsv},
				{ "lunar_special",     &get_lunar_parent_coordsv},
				{ "mars_special",      &get_mars_helio_coordsv},
				{ "phobos_special",    &get_phobos_parent_coordsv},
				{ "deimos_special",    &get_deimos_parent_coordsv},
				{ "jupiter_special",   &get_jupiter_helio_coordsv},
				{ "io_special",        &get_io_parent_coordsv},
				{ "europa_special",    &get_europa_parent_coordsv},
				{ "ganymede_special",  &get_ganymede_parent_coordsv},
				{ "calisto_special",   &get_callisto_parent_coordsv},
				{ "callisto_special",  &get_callisto_parent_coordsv},
				{ "saturn_special",    &get_saturn_helio_coordsv},
				{ "mimas_special",     &get_mimas_parent_coordsv},
				{ "enceladus_special", &get_enceladus_parent_coordsv},
				{ "tethys_special",    &get_tethys_parent_coordsv},
				{ "dione_special",     &get_dione_parent_coordsv},
				{ "rhea_special",      &get_rhea_parent_coordsv},
				{ "titan_special",     &get_titan_parent_coordsv},
				{ "hyperion_special",  &get_hyperion_parent_coordsv},
				{ "iapetus_special",   &get_iapetus_parent_coordsv},
				{ "helene_special",    &get_helene_parent_coordsv},
				{ "telesto_special",   &get_telesto_parent_coordsv},
				{ "calypso_special",   &get_calypso_parent_coordsv},
				{ "uranus_special",    &get_uranus_helio_coordsv},
				{ "miranda_special",   &get_miranda_parent_coordsv},
				{ "ariel_special",     &get_ariel_parent_coordsv},
				{ "umbriel_special",   &get_umbriel_parent_coordsv},
				{ "titania_special",   &get_titania_parent_coordsv},
				{ "oberon_special",    &get_oberon_parent_coordsv},
				{ "neptune_special",   &get_neptune_helio_coordsv},
				{ "pluto_special",     &get_pluto_helio_coordsv}};
			static const QMap<QString, OsculatingFunctType*>osculatingMap={
				{ "mercury_special",   &get_mercury_helio_osculating_coords},
				{ "venus_special",     &get_venus_helio_osculating_coords},
				{ "earth_special",     &get_earth_helio_osculating_coords},
				{ "mars_special",      &get_mars_helio_osculating_coords},
				{ "jupiter_special",   &get_jupiter_helio_osculating_coords},
				{ "saturn_special",    &get_saturn_helio_osculating_coords},
				{ "uranus_special",    &get_uranus_helio_osculating_coords},
				{ "neptune_special",   &get_neptune_helio_osculating_coords}};
			posfunc=posfuncMap.value(coordFuncName, Q_NULLPTR);
			osculatingFunc=osculatingMap.value(coordFuncName, Q_NULLPTR);
		}
		if (posfunc==Q_NULLPTR)
		{
			qCritical() << "ERROR in section " << secname << ": can't find posfunc " << coordFuncName << " for " << englishName;
			exit(-1);
		}

		// Create the Solar System body and add it to the list
		//TODO: Refactor the subclass selection to reduce duplicate code mess here,
		// by at least using this base class pointer and using setXXX functions instead of mega-constructors
		// that have to pass most of it on to the Planet class
		PlanetP newP;

		// New class objects, named "plutino", "cubewano", "dwarf planet", "SDO", "OCO", has properties
		// similar to asteroids and we should calculate their positions like for asteroids. Dwarf planets
		// have one exception: Pluto - as long as we use a special function for calculation of Pluto's orbit.
		if ((type == L1S("asteroid") || type == L1S("dwarf planet") || type == L1S("cubewano") || type==L1S("sednoid") || type == L1S("plutino") || type == L1S("scattered disc object") || type == L1S("Oort cloud object") || type == L1S("interstellar object")) && !englishName.contains("Pluto"))
		{
			minorBodies << englishName;

			Vec3f color = Vec3f(1.f, 1.f, 1.f);
			if (bV<99.)
				color = skyDrawer->indexToColor(BvToColorIndex(bV))*0.75f; // see ZoneArray.cpp:L490
			else
				color = Vec3f(pd.value(secname+"/color", "1.0,1.0,1.0").toString());

			const bool hidden = pd.value(secname+"/hidden", false).toBool();
			const QString normalMapName = ( hidden ? "" : englishName.toLower().append("_normals.png")); // no normal maps for invisible objects!
			const QString horizonMapName = ( hidden ? "" : englishName.toLower().append("_normals.png")); // no normal maps for invisible objects!

			newP = PlanetP(new MinorPlanet(englishName,
						    pd.value(secname+"/radius", 0.0).toDouble()/AU,
						    pd.value(secname+"/oblateness", 0.0).toDouble(),
						    color, // halo color
						    pd.value(secname+"/albedo", 0.0f).toFloat(),
						    pd.value(secname+"/roughness",0.9f).toFloat(),
						    pd.value(secname+"/tex_map", "nomap.png").toString(),
						    pd.value(secname+"/normals_map", normalMapName).toString(),
						    pd.value(secname+"/horizon_map", horizonMapName).toString(),
						    pd.value(secname+"/model").toString(),
						    posfunc,
						    static_cast<KeplerOrbit*>(orbitPtr), // the KeplerOrbit object created previously
						    osculatingFunc, // should be Q_NULLPTR
						    closeOrbit,
						    hidden,
						    type));
			QSharedPointer<MinorPlanet> mp =  newP.dynamicCast<MinorPlanet>();
			//Number, IAU provisional designation
			mp->setMinorPlanetNumber(pd.value(secname+"/minor_planet_number", 0).toInt());
			mp->setIAUDesignation(pd.value(secname+"/iau_designation", "").toString());

			//H-G magnitude system
			const float magnitude = pd.value(secname+"/absolute_magnitude", -99.f).toFloat();
			const float slope = pd.value(secname+"/slope_parameter", 0.15f).toFloat();
			if (magnitude > -99.f)
			{
				mp->setAbsoluteMagnitudeAndSlope(magnitude, qBound(0.0f, slope, 1.0f));
				mp->updateEquatorialRadius();
			}

			mp->setColorIndexBV(static_cast<float>(bV));
			mp->setSpectralType(pd.value(secname+"/spec_t", "").toString(), pd.value(secname+"/spec_b", "").toString());

			// Discovery circumstances
			QString discovererName = pd.value(secname+"/discoverer", "").toString();
			QString discoveryDate = pd.value(secname+"/discovery", "").toString();
			if (!discoveryDate.isEmpty())
				mp->setDiscoveryData(discoveryDate, discovererName);

			// order of codes: date_code [P/1982 U1] - perihelion_code [1986 III] - discovery_code [1982i]
			QStringList codes = { pd.value(secname+"/date_code", "").toString(),
					      pd.value(secname+"/perihelion_code", "").toString(),
					      pd.value(secname+"/discovery_code", "").toString() };
			codes.removeAll("");
			if (codes.count()>0)
				mp->setExtraDesignations(codes);

			if (semi_major_axis>0)
				mp->deltaJDE = 2.0*semi_major_axis*StelCore::JD_SECOND;
			 else if (semi_major_axis<=0.0 && type!=L1S("interstellar object"))
				qWarning().noquote() << "Minor body" << englishName << "has no semimajor axis!";

			systemMinorBodies.push_back(newP);
		}
		else if (type == L1S("comet"))
		{
			minorBodies << englishName;
			newP = PlanetP(new Comet(englishName,
					      pd.value(secname+"/radius", 5.0).toDouble()/AU,
					      pd.value(secname+"/oblateness", 0.0).toDouble(),
					      Vec3f(pd.value(secname+"/color", "1.0,1.0,1.0").toString()), // halo color
					      pd.value(secname+"/albedo", 0.075f).toFloat(), // assume very dark surface
					      pd.value(secname+"/roughness",0.9f).toFloat(),
					      pd.value(secname+"/outgas_intensity",0.1f).toFloat(),
					      pd.value(secname+"/outgas_falloff", 0.1f).toFloat(),
					      pd.value(secname+"/tex_map", "nomap.png").toString(),
					      pd.value(secname+"/model").toString(),
					      posfunc,
					      static_cast<KeplerOrbit*>(orbitPtr), // the KeplerOrbit object
					      osculatingFunc, // ALWAYS NULL for comets.
					      closeOrbit,
					      pd.value(secname+"/hidden", false).toBool(),
					      type,
					      pd.value(secname+"/dust_widthfactor", 1.5f).toFloat(),
					      pd.value(secname+"/dust_lengthfactor", 0.4f).toFloat(),
					      pd.value(secname+"/dust_brightnessfactor", 1.5f).toFloat()
					      ));
			QSharedPointer<Comet> comet = newP.dynamicCast<Comet>();

			//g,k magnitude system
			const float magnitude = pd.value(secname+"/absolute_magnitude", -99.f).toFloat();
			const float slope = qBound(-5.0f, pd.value(secname+"/slope_parameter", 4.0f).toFloat(), 30.0f);
			if (magnitude > -99.f)
					comet->setAbsoluteMagnitudeAndSlope(magnitude, slope);

			QString iauDesignation = pd.value(secname+"/iau_designation", "").toString();
			if (!iauDesignation.isEmpty())
				comet->setIAUDesignation(iauDesignation);
			// order of codes: date_code [P/1982 U1] - perihelion_code [1986 III] - discovery_code [1982i]
			QStringList codes = { pd.value(secname+"/date_code", "").toString(),
					      pd.value(secname+"/perihelion_code", "").toString(),
					      pd.value(secname+"/discovery_code", "").toString() };
			codes.removeAll("");
			if (codes.count()>0)
				comet->setExtraDesignations(codes);

			// Discovery circumstances
			QString discovererName = pd.value(secname+"/discoverer", "").toString();
			QString discoveryDate = pd.value(secname+"/discovery", "").toString();
			if (!discoveryDate.isEmpty())
				comet->setDiscoveryData(discoveryDate, discovererName);

			systemMinorBodies.push_back(newP);
		}
		else // type==star|planet|moon|dwarf planet|observer|artificial
		{
			//qDebug() << type;
			Q_ASSERT(type==L1S("star") || type==L1S("planet") || type==L1S("moon") || type==L1S("artificial") || type==L1S("observer") || type==L1S("dwarf planet")); // TBD: remove Pluto...
			// Set possible default name of the normal map for avoiding yin-yang shaped moon
			// phase when normal map key not exists. Example: moon_normals.png
			// Details: https://bugs.launchpad.net/stellarium/+bug/1335609			
			newP = PlanetP(new Planet(englishName,
					       pd.value(secname+"/radius", 1.0).toDouble()/AU,
					       pd.value(secname+"/oblateness", 0.0).toDouble(),
					       Vec3f(pd.value(secname+"/color", "1.0,1.0,1.0").toString()), // halo color
					       pd.value(secname+"/albedo", 0.25f).toFloat(),
					       pd.value(secname+"/roughness",0.9f).toFloat(),
					       pd.value(secname+"/tex_map", "nomap.png").toString(),
					       pd.value(secname+"/normals_map", englishName.toLower().append("_normals.png")).toString(),
					       pd.value(secname+"/horizon_map", englishName.toLower().append("_horizon.png")).toString(),
					       pd.value(secname+"/model").toString(),
					       posfunc,
					       static_cast<KeplerOrbit*>(orbitPtr), // This remains Q_NULLPTR for the major planets, or has a KeplerOrbit for planet moons.
					       osculatingFunc,
					       closeOrbit,
					       pd.value(secname+"/hidden", false).toBool(),
					       pd.value(secname+"/atmosphere", false).toBool(),
					       pd.value(secname+"/halo", true).toBool(),
					       type));
			newP->absoluteMagnitude = pd.value(secname+"/absolute_magnitude", -99.f).toFloat();
			newP->massKg = pd.value(secname+"/mass_kg", 0.).toDouble();

			// Moon designation (planet index + IAU moon number)
			QString moonDesignation = pd.value(secname+"/iau_moon_number", "").toString();
			if (!moonDesignation.isEmpty())
				newP->setIAUMoonNumber(moonDesignation);
			newP->setColorIndexBV(static_cast<float>(bV));
			// Discovery circumstances
			QString discovererName = pd.value(secname+"/discoverer", "").toString();
			QString discoveryDate = pd.value(secname+"/discovery", "").toString();
			if (!discoveryDate.isEmpty())
				newP->setDiscoveryData(discoveryDate, discovererName);
		}

		if (!parent.isNull())
		{
			parent->satellites.append(newP);
			newP->parent = parent;
		}
		if (secname==L1S("earth")) earth = newP;
		if (secname==L1S("sun")) sun = newP;
		if (secname==L1S("moon")) moon = newP;

		// At this point the orbit and object type (class Planet and subclasses) have been fixed.
		// For many objects we have oriented spheroids with rotational parameters.

		// There are two ways of defining the axis orientation:
		// obliquity and ascending node, which was used by Stellarium already before 2010 (based on Celestia?).
		double rotObliquity = pd.value(secname+"/rot_obliquity",0.).toDouble()*(M_PI_180);
		double rotAscNode = pd.value(secname+"/rot_equator_ascending_node",0.).toDouble()*(M_PI_180);
		// rot_periode given in hours (from which rotPeriod in days),
		// The default is useful for many moons in bound rotation
		double rotPeriod=pd.value(secname+"/rot_periode", pd.value(secname+"/orbit_Period", 1.).toDouble()*24.).toDouble()/24.;
		double rotOffset=pd.value(secname+"/rot_rotation_offset",0.).toDouble();

		// 0.21+: Use WGCCRE planet North pole data if available
		// NB: N pole for J2000 epoch as defined by IAU (NOT right hand rotation rule)
		// Define only basic motion. Use special functions for more complicated axes.
		const double J2000NPoleRA  = pd.value(secname+"/rot_pole_ra",  0.).toDouble()*M_PI_180;
		const double J2000NPoleRA1 = pd.value(secname+"/rot_pole_ra1", 0.).toDouble()*M_PI_180;
		const double J2000NPoleDE  = pd.value(secname+"/rot_pole_de",  0.).toDouble()*M_PI_180;
		const double J2000NPoleDE1 = pd.value(secname+"/rot_pole_de1", 0.).toDouble()*M_PI_180;
		const double J2000NPoleW0  = pd.value(secname+"/rot_pole_w0",  0.).toDouble(); // [degrees]   Basically the same idea as rot_rotation_offset, but W!=rotAngle
		const double J2000NPoleW1  = pd.value(secname+"/rot_pole_w1",  0.).toDouble(); // [degrees/d] Basically the same idea as 360/rot_periode
		if (fabs(J2000NPoleW1) > 0.0) // Patch possibly old period value with a more modern value.
		{
			// this is just another expression for rotational speed.
			rotPeriod=360.0/J2000NPoleW1;
		}

		// IMPORTANT: For the planet moons with orbits relative to planets' equator plane,
		// re-compute the important bits from the updated axis elements.
		// Reactivated to re-establish Pluto/Charon lock #153
		if((J2000NPoleRA!=0.) || (J2000NPoleDE!=0.))
		{
			// If available, recompute obliquity and AscNode from the new data.
			// Solution since 0.16: Make this once for J2000.
			// Optional (future?): Repeat this block in Planet::computeTransMatrix() for planets with moving axes and update all Moons' KeplerOrbit if required.
			Vec3d J2000NPole;
			StelUtils::spheToRect(J2000NPoleRA,J2000NPoleDE,J2000NPole);

			Vec3d vsop87Pole(StelCore::matJ2000ToVsop87.multiplyWithoutTranslation(J2000NPole));

			double lon, lat;
			StelUtils::rectToSphe(&lon, &lat, vsop87Pole);

			rotObliquity = (M_PI_2 - lat);
			rotAscNode = (lon + M_PI_2);

			//qDebug() << englishName << ": Compare these values to the older data in ssystem_major";
			//qDebug() << "\tCalculated rotational obliquity: " << rotObliquity*180./M_PI;
			//qDebug() << "\tCalculated rotational ascending node: " << rotAscNode*180./M_PI;

			if (J2000NPoleW0 >0)
			{
				// W0 is counted from the ascending node with ICRF, but rotOffset from orbital plane.
				// Try this assumption by just counting Offset=W0+90+RA0.
				rotOffset=J2000NPoleW0 + lon*M_180_PI;
				//qDebug() << "\tCalculated rotational period (days // hours): " << rotPeriod << "//" << rotPeriod*24.;
				//qDebug() << "\tRotational offset (degrees): " << rotOffset;
			}
		}
		newP->setRotationElements(
			englishName,
			rotPeriod,
			rotOffset,
			pd.value(secname+"/rot_epoch", Planet::J2000).toDouble(),
			rotObliquity,
			rotAscNode,
			J2000NPoleRA,
			J2000NPoleRA1,
			J2000NPoleDE,
			J2000NPoleDE1,
			J2000NPoleW0,
			J2000NPoleW1);
		// orbit_Period given in days.
		// Elliptical Kepler orbits (ecc<0.9) will replace whatever is given by a value computed on the fly. Parabolic objects show 0.
		newP->setSiderealPeriod(fabs(pd.value(secname+"/orbit_Period", 0.).toDouble()));

		if (pd.contains(secname+"/tex_ring")) {
			const float rMin = pd.value(secname+"/ring_inner_size").toFloat()/AUf;
			const float rMax = pd.value(secname+"/ring_outer_size").toFloat()/AUf;
			Ring *r = new Ring(rMin,rMax,pd.value(secname+"/tex_ring").toString());
			newP->setRings(r);
		}

		systemPlanets.push_back(newP);
		readOk++;
	}

	// special case: load earth shadow texture
	if (!Planet::texEarthShadow)
		Planet::texEarthShadow = StelApp::getInstance().getTextureManager().createTexture(StelFileMgr::getInstallationDir()+"/textures/earth-shadow.png");

	// Also comets just have static textures.
	if (!Comet::comaTexture)
		Comet::comaTexture = StelApp::getInstance().getTextureManager().createTextureThread(StelFileMgr::getInstallationDir()+"/textures/cometComa.png", StelTexture::StelTextureParams(true, GL_LINEAR, GL_CLAMP_TO_EDGE));
	//tail textures. We use paraboloid tail bodies, textured like a fisheye sphere, i.e. center=head. The texture should be something like a mottled star to give some structure.
	if (!Comet::tailTexture)
		Comet::tailTexture = StelApp::getInstance().getTextureManager().createTextureThread(StelFileMgr::getInstallationDir()+"/textures/cometTail.png", StelTexture::StelTextureParams(true, GL_LINEAR, GL_CLAMP_TO_EDGE));

	if (readOk==0)
		qWarning().noquote() << "No Solar System objects loaded from" << QDir::toNativeSeparators(filePath);
	else
		qInfo().noquote() << "Loaded" << readOk << "Solar System bodies from " << filePath;
	qInfo().noquote() << "Solar System now has" << systemPlanets.count() << "entries.";
	return readOk>0;
}

// Compute the position for every elements of the solar system.
// The order is not important since the position is computed relatively to the mother body
void SolarSystem::computePositions(StelCore *core, double dateJDE, PlanetP observerPlanet)
{
	const StelObserver *obs=core->getCurrentObserver();
	const bool withAberration=core->getUseAberration();
	// We distribute computing over a few threads from the current threadpool, but also compute one block in the main thread so that this does not starve.
	// Given the comparably low impact of planetary positions on the overall frame time, we usually don't need more than about 4 extra threads. (Profiled with 12.000 objects.)
	static bool threadMessage=true;
	if (threadMessage)
	{
		qInfo() << "SolarSystem: We have configured" << extraThreads << "threads (plus main thread) for computePositions().";
		if (extraThreads > QThreadPool::globalInstance()->maxThreadCount()-1)
		{
			qWarning() << "This is more than the maximum additional thread count (" << QThreadPool::globalInstance()->maxThreadCount()-1 << ").";
			qWarning() << "Consider reducing this number to avoid oversubscription.";
		}
		threadMessage=false;
	}

	if (flagLightTravelTime) // switching off light time correction implies no aberration for the planets.
	{
		const StelObserver *obs=core->getCurrentObserver();
		const bool observerPlanetIsEarth = observerPlanet==getEarth();
		//static StelObjectMgr* omgr=GETSTELMODULE(StelObjectMgr);
		//omgr->removeExtraInfoStrings(StelObject::DebugAid);

		switch (computePositionsAlgorithm)
		{
		case 3: // Ruslan's 1-loop solution. This would be faster, but has problems with moons when the respective planet has not been computed yet.
		{
			// Position of this planet will be used in the subsequent computations
			observerPlanet->computePosition(obs, dateJDE, Vec3d(0.));
			const bool observerIsEarth = observerPlanet->englishName==L1S("Earth");
			const Vec3d obsPosJDE=observerPlanet->getHeliocentricEclipticPos();
			const Vec3d aberrationPushSpeed=observerPlanet->getHeliocentricEclipticVelocity() * core->getAberrationFactor();
			const double dateJD = dateJDE - (core->computeDeltaT(dateJDE))/86400.0;

			const auto processPlanet = [this,dateJD,dateJDE,observerIsEarth,withAberration,observerPlanet,
						   obsPosJDE,aberrationPushSpeed,obs](const PlanetP& p, const Vec3d& observerPosFinal)
			{
				// 1. First approximation.
				p->computePosition(obs, dateJDE, Vec3d(0.));

				// For higher accuracy, we now make two iterations of light time and aberration correction. In the final
				// round, we also compute rotation data.  May fix sub-arcsecond inaccuracies, and optionally apply
				// aberration in the way described in Explanatory Supplement (2013), 7.55.  For reasons unknown (See
				// discussion in GH:#1626) we do not add anything for the Moon when observed from Earth!  Presumably the
				// used ephemerides already provide aberration-corrected positions for the Moon?
				Vec3d planetPos = p->getHeliocentricEclipticPos();
				double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
				const bool needToApplyAberration = withAberration && (!observerIsEarth || p != getMoon());
				Vec3d aberrationPush(0.);
				if(needToApplyAberration)
					aberrationPush=lightTimeDays*aberrationPushSpeed;
				p->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);

				// Extra accuracy with another round. Not sure if useful. Maybe hide behind a new property flag?
				planetPos = p->getHeliocentricEclipticPos();
				lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
				if(needToApplyAberration)
					aberrationPush=lightTimeDays*aberrationPushSpeed;
				// The next call may already do nothing if the time difference to the previous round is not large enough.
				p->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);

				const auto update = &RotationElements::updatePlanetCorrections;
				if      (p->englishName==L1S("Moon"))    update(dateJDE-lightTimeDays, RotationElements::EarthMoon);
				else if (p->englishName==L1S("Mars"))    update(dateJDE-lightTimeDays, RotationElements::Mars);
				else if (p->englishName==L1S("Jupiter")) update(dateJDE-lightTimeDays, RotationElements::Jupiter);
				else if (p->englishName==L1S("Saturn"))  update(dateJDE-lightTimeDays, RotationElements::Saturn);
				else if (p->englishName==L1S("Uranus"))  update(dateJDE-lightTimeDays, RotationElements::Uranus);
				else if (p->englishName==L1S("Neptune")) update(dateJDE-lightTimeDays, RotationElements::Neptune);

				if(p != observerPlanet)
				{
					const double light_speed_correction = (AU / (SPEED_OF_LIGHT * 86400)) *
									      (p->getHeliocentricEclipticPos()-obsPosJDE).norm();
					p->computeTransMatrix(dateJD-light_speed_correction, dateJDE-light_speed_correction);
				}
			};

			// This will be used for computation of transformation matrices
			processPlanet(observerPlanet, Vec3d(0.));
			observerPlanet->computeTransMatrix(dateJD, dateJDE);
			const Vec3d observerPosFinal = observerPlanet->getHeliocentricEclipticPos();

			// Threadable loop function for self-set number of additional worker threads
			const auto loop = [&planets=std::as_const(systemPlanets),processPlanet,
					  observerPosFinal](const int indexMin, const int indexMax)
			{
				for(int i = indexMin; i <= indexMax; ++i)
					processPlanet(planets[i], observerPosFinal);
			};

			QList<QFuture<void>> futures;
			const int totalThreads = extraThreads+1;
			const auto blockSize = systemPlanets.size() / totalThreads;
			for(int threadN=0; threadN<totalThreads-1; ++threadN)
			{
				const int indexMin = blockSize*threadN;
				const int indexMax = blockSize*(threadN+1)-1;
				futures.append(QtConcurrent::run(loop, indexMin,indexMax));
			}
			// and the last thread is the current one
			loop(blockSize*(totalThreads-1), systemPlanets.size()-1);
			for(auto& f : futures)

				f.waitForFinished();
		}
		break;
		case 2:
		{
			// Better 3-loop solution. This is still following the original solution:
			// First, compute approximate positions at JDE.
			// Then for each object, compute light time and repeat light-time corrected.
			// Third, check new light time, and recompute once more if needed.

			// 1. First approximation.
			QList<QFuture<void>> futures;
			// This defines a function to be thrown onto a pool thread that computes every 'incr'th element.
			auto plCompLoopZero = [=](int offset){
				for (auto it=systemPlanets.cbegin()+offset, end=systemPlanets.cend(); it<end; it+=(extraThreads+1))
				{
					it->data()->computePosition(obs, dateJDE, Vec3d(0.));
				}
			};

			// Move to external threads, but also run a part in the main thread. The index 'availableThreads' is just the last group of objects.
			for (int stride=0; stride<extraThreads; stride++)
			{
				auto future=QtConcurrent::run(plCompLoopZero, stride);
				futures.append(future);
			}
			plCompLoopZero(extraThreads);

			// Now the list is being computed by other threads. we can just wait sequentially for completion.
			for(auto f: futures)
				f.waitForFinished();
			futures.clear();

			const Vec3d &obsPosJDE=observerPlanet->getHeliocentricEclipticPos();

			// 2.&3.: For higher accuracy, we now make two iterations of light time and aberration correction. In the final
			// round, we also compute rotation data.  May fix sub-arcsecond inaccuracies, and optionally apply
			// aberration in the way described in Explanatory Supplement (2013), 7.55.  For reasons unknown (See
			// discussion in GH:#1626) we do not add anything for the Moon when observed from Earth!  Presumably the
			// used ephemerides already provide aberration-corrected positions for the Moon?
			const Vec3d aberrationPushSpeed=observerPlanet->getHeliocentricEclipticVelocity() * core->getAberrationFactor();

			auto plCompLoopOne = [=](int offset){
				for (auto it=systemPlanets.cbegin()+offset, end=systemPlanets.cend(); it<end; it+=extraThreads+1)
				{
					const auto planetPos = it->data()->getHeliocentricEclipticPos();
					const double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
					Vec3d aberrationPush(0.);
					if (withAberration && (!observerPlanetIsEarth || it->data() != getMoon()))
						aberrationPush=lightTimeDays*aberrationPushSpeed;
					it->data()->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);
				}
			};
			for (int stride=0; stride<extraThreads; stride++)
			{
				auto future=QtConcurrent::run(plCompLoopOne, stride);
				futures.append(future);
			}
			plCompLoopOne(extraThreads); // main thread's share of the computation task
			// Now the list is being computed by other threads. we can just wait sequentially for completion.
			for(auto f: futures)
				f.waitForFinished();
			futures.clear();

			// 3. Extra accuracy with another round. Not sure if useful. Maybe hide behind a new property flag?
			auto plCompLoopTwo = [=](int offset){
				for (auto it=systemPlanets.cbegin()+offset, end=systemPlanets.cend(); it<end; it+=extraThreads+1)
				{
					const auto planetPos = it->data()->getHeliocentricEclipticPos();
					const double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
					Vec3d aberrationPush(0.);
					if (withAberration && (!observerPlanetIsEarth || it->data() != getMoon()))
						aberrationPush=lightTimeDays*aberrationPushSpeed;
					// The next call may already do nothing if the time difference to the previous round is not large enough.
					it->data()->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);
					//it->data()->setExtraInfoString(StelObject::DebugAid, QString("LightTime %1d; obsSpeed %2/%3/%4 AU/d")
					//							.arg(QString::number(lightTimeDays, 'f', 3))
					//							.arg(QString::number(aberrationPushSpeed[0], 'f', 3))
					//							.arg(QString::number(aberrationPushSpeed[1], 'f', 3))
					//							.arg(QString::number(aberrationPushSpeed[2], 'f', 3)));

					const auto update = &RotationElements::updatePlanetCorrections;
					if      (it->data()->englishName==L1S("Moon"))    update(dateJDE-lightTimeDays, RotationElements::EarthMoon);
					else if (it->data()->englishName==L1S("Mars"))    update(dateJDE-lightTimeDays, RotationElements::Mars);
					else if (it->data()->englishName==L1S("Jupiter")) update(dateJDE-lightTimeDays, RotationElements::Jupiter);
					else if (it->data()->englishName==L1S("Saturn"))  update(dateJDE-lightTimeDays, RotationElements::Saturn);
					else if (it->data()->englishName==L1S("Uranus"))  update(dateJDE-lightTimeDays, RotationElements::Uranus);
					else if (it->data()->englishName==L1S("Neptune")) update(dateJDE-lightTimeDays, RotationElements::Neptune);
				}
			};
			for (int stride=0; stride<extraThreads; stride++)
			{
				auto future=QtConcurrent::run(plCompLoopTwo, stride);
				futures.append(future);
			}
			// At this point all available threads from the global ThreadPool should be active:
			//omgr->addToExtraInfoString(StelObject::DebugAid, QString("Threads: Ideal: %1, Pool max %2/active %3, SolarSystem using %4<br/>").
			//			   arg(QString::number(QThread::idealThreadCount()),
			//			       QString::number(QThreadPool::globalInstance()->maxThreadCount()),
			//			       QString::number(QThreadPool::globalInstance()->activeThreadCount()),
			//			       QString::number(extraThreads)));
			// and we still run the last stride in the main thread.
			plCompLoopTwo(extraThreads);
			// Now the list is being computed by other threads. we can just wait sequentially for completion.
			for(auto f: futures)
				f.waitForFinished();
			computeTransMatrices(dateJDE, observerPlanet->getHeliocentricEclipticPos());
		}
		break;
		case 1: // Simple multithreading with QtConcurrent::blockingMap(). 3-loop solution. This is closely following the original solution:
		{
			// First, compute approximate positions at JDE.
			// Then for each object, compute light time and repeat light-time corrected.
			// Third, check new light time, and recompute if needed.

			// 1. First approximation.
			auto plCompPosJDEZero = [=](QSharedPointer<Planet> &pl){pl->computePosition(obs, dateJDE, Vec3d(0.));};
			QtConcurrent::blockingMap(systemPlanets, plCompPosJDEZero);

			const Vec3d &obsPosJDE=observerPlanet->getHeliocentricEclipticPos();

			// 2&3. For higher accuracy, we now make two iterations of light time and aberration correction. In the final
			// round, we also compute rotation data.  May fix sub-arcsecond inaccuracies, and optionally apply
			// aberration in the way described in Explanatory Supplement (2013), 7.55.  For reasons unknown (See
			// discussion in GH:#1626) we do not add anything for the Moon when observed from Earth!  Presumably the
			// used ephemerides already provide aberration-corrected positions for the Moon?
			const Vec3d aberrationPushSpeed=observerPlanet->getHeliocentricEclipticVelocity() * core->getAberrationFactor();

			auto plCompPosJDEOne = [=](QSharedPointer<Planet> &p){
				const Vec3d planetPos = p->getHeliocentricEclipticPos();
				const double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
				Vec3d aberrationPush(0.);
				if (withAberration && (!observerPlanetIsEarth || p != getMoon() ))
					aberrationPush=lightTimeDays*aberrationPushSpeed;
				p->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);
			};
			QtConcurrent::blockingMap(systemPlanets, plCompPosJDEOne);

			// 3. Extra accuracy with another round. Not sure if useful. Maybe hide behind a new property flag?
			auto plCompPosJDETwo = [=](QSharedPointer<Planet> &p){
				const Vec3d planetPos = p->getHeliocentricEclipticPos();
				const double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
				Vec3d aberrationPush(0.);
				if (withAberration && (!observerPlanetIsEarth || p != getMoon() ))
					aberrationPush=lightTimeDays*aberrationPushSpeed;
				// The next call may already do nothing if the time difference to the previous round is not large enough.
				p->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);
				//p->setExtraInfoString(StelObject::DebugAid, QString("LightTime %1d; obsSpeed %2/%3/%4 AU/d")
				//					      .arg(QString::number(lightTimeDays, 'f', 3))
				//					      .arg(QString::number(aberrationPushSpeed[0], 'f', 3))
				//					      .arg(QString::number(aberrationPushSpeed[1], 'f', 3))
				//					      .arg(QString::number(aberrationPushSpeed[2], 'f', 3)));

				const auto update = &RotationElements::updatePlanetCorrections;
				if      (p->englishName==L1S("Moon"))    update(dateJDE-lightTimeDays, RotationElements::EarthMoon);
				else if (p->englishName==L1S("Mars"))    update(dateJDE-lightTimeDays, RotationElements::Mars);
				else if (p->englishName==L1S("Jupiter")) update(dateJDE-lightTimeDays, RotationElements::Jupiter);
				else if (p->englishName==L1S("Saturn"))  update(dateJDE-lightTimeDays, RotationElements::Saturn);
				else if (p->englishName==L1S("Uranus"))  update(dateJDE-lightTimeDays, RotationElements::Uranus);
				else if (p->englishName==L1S("Neptune")) update(dateJDE-lightTimeDays, RotationElements::Neptune);
			};
			QtConcurrent::blockingMap(systemPlanets, plCompPosJDETwo);
			computeTransMatrices(dateJDE, observerPlanet->getHeliocentricEclipticPos());
		}
		break;
		case 0:
		default: // Original single-threaded.
		{
			// First, compute approximate positions at JDE.
			// Then for each object, compute light time and repeat light-time corrected.
			// Third, check new light time, and recompute if needed.

			for (const auto& p : std::as_const(systemPlanets))
			{
				p->computePosition(obs, dateJDE, Vec3d(0.));
			}
			const Vec3d obsPosJDE=observerPlanet->getHeliocentricEclipticPos();

			// For higher accuracy, we now make two iterations of light time and aberration correction. In the final
			// round, we also compute rotation data.  May fix sub-arcsecond inaccuracies, and optionally apply
			// aberration in the way described in Explanatory Supplement (2013), 7.55.  For reasons unknown (See
			// discussion in GH:#1626) we do not add anything for the Moon when observed from Earth!  Presumably the
			// used ephemerides already provide aberration-corrected positions for the Moon?
			const Vec3d aberrationPushSpeed=observerPlanet->getHeliocentricEclipticVelocity() * core->getAberrationFactor();
			for (const auto& p : std::as_const(systemPlanets))
			{
				//p->setExtraInfoString(StelObject::DebugAid, "");
				const auto planetPos = p->getHeliocentricEclipticPos();
				const double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
				Vec3d aberrationPush(0.);
				if (withAberration && (observerPlanet->englishName!=L1S("Earth") || p->englishName!=L1S("Moon")))
					aberrationPush=lightTimeDays*aberrationPushSpeed;
				p->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);
			}
			// Extra accuracy with another round. Not sure if useful. Maybe hide behind a new property flag?
			for (const auto& p : std::as_const(systemPlanets))
			{
				//p->setExtraInfoString(StelObject::DebugAid, "");
				const auto planetPos = p->getHeliocentricEclipticPos();
				const double lightTimeDays = (planetPos-obsPosJDE).norm() * (AU / (SPEED_OF_LIGHT * 86400.));
				Vec3d aberrationPush(0.);
				if (withAberration && (observerPlanet->englishName!=L1S("Earth") || p->englishName!=L1S("Moon")))
					aberrationPush=lightTimeDays*aberrationPushSpeed;
				// The next call may already do nothing if the time difference to the previous round is not large enough.
				p->computePosition(obs, dateJDE-lightTimeDays, aberrationPush);
				//p->setExtraInfoString(StelObject::DebugAid, QString("LightTime %1d; obsSpeed %2/%3/%4 AU/d")
				//					      .arg(QString::number(lightTimeDays, 'f', 3))
				//					      .arg(QString::number(aberrationPushSpeed[0], 'f', 3))
				//					      .arg(QString::number(aberrationPushSpeed[0], 'f', 3))
				//					      .arg(QString::number(aberrationPushSpeed[0], 'f', 3)));

				const auto update = &RotationElements::updatePlanetCorrections;
				if      (p->englishName==L1S("Moon"))    update(dateJDE-lightTimeDays, RotationElements::EarthMoon);
				else if (p->englishName==L1S("Mars"))    update(dateJDE-lightTimeDays, RotationElements::Mars);
				else if (p->englishName==L1S("Jupiter")) update(dateJDE-lightTimeDays, RotationElements::Jupiter);
				else if (p->englishName==L1S("Saturn"))  update(dateJDE-lightTimeDays, RotationElements::Saturn);
				else if (p->englishName==L1S("Uranus"))  update(dateJDE-lightTimeDays, RotationElements::Uranus);
				else if (p->englishName==L1S("Neptune")) update(dateJDE-lightTimeDays, RotationElements::Neptune);
			}
			computeTransMatrices(dateJDE, observerPlanet->getHeliocentricEclipticPos());
		} // end of default (original single-threaded) solution
		}
	}
	else
	{
		for (const auto& p : std::as_const(systemPlanets))
		{
			//p->setExtraInfoString(StelObject::DebugAid, "");
			p->computePosition(obs, dateJDE, Vec3d(0.));
			const auto update = &RotationElements::updatePlanetCorrections;
			if      (p->englishName==L1S("Moon"))    update(dateJDE, RotationElements::EarthMoon);
			else if (p->englishName==L1S("Mars"))    update(dateJDE, RotationElements::Mars);
			else if (p->englishName==L1S("Jupiter")) update(dateJDE, RotationElements::Jupiter);
			else if (p->englishName==L1S("Saturn"))  update(dateJDE, RotationElements::Saturn);
			else if (p->englishName==L1S("Uranus"))  update(dateJDE, RotationElements::Uranus);
			else if (p->englishName==L1S("Neptune")) update(dateJDE, RotationElements::Neptune);
		}
		computeTransMatrices(dateJDE, observerPlanet->getHeliocentricEclipticPos());
	}
}

// Compute the transformation matrix for every elements of the solar system.
// The elements have to be ordered hierarchically, eg. it's important to compute earth before moon.
void SolarSystem::computeTransMatrices(double dateJDE, const Vec3d& observerPos)
{
	const double dateJD=dateJDE - (StelApp::getInstance().getCore()->computeDeltaT(dateJDE))/86400.0;

	if (flagLightTravelTime)
	{
		for (const auto& p : std::as_const(systemPlanets))
		{
			const double light_speed_correction = (p->getHeliocentricEclipticPos()-observerPos).norm() * (AU / (SPEED_OF_LIGHT * 86400));
			p->computeTransMatrix(dateJD-light_speed_correction, dateJDE-light_speed_correction);
		}
	}
	else
	{
		for (const auto& p : std::as_const(systemPlanets))
		{
			p->computeTransMatrix(dateJD, dateJDE);
		}
	}
}

// And sort them from the furthest to the closest to the observer
// NOTE: std::binary_function is deprecated in C++11 and removed in C++17
struct biggerDistance : public StelUtils::binary_function<PlanetP, PlanetP, bool>
{
	bool operator()(PlanetP p1, PlanetP p2)
	{
		return p1->getDistance() > p2->getDistance();
	}
};

// Draw all the elements of the solar system
// We are supposed to be in heliocentric coordinate
void SolarSystem::draw(StelCore* core)
{
	// AstroCalcDialog
	drawEphemerisItems(core);

	if (!flagShow)
		return;
	static StelObjectMgr *sObjMgr=GETSTELMODULE(StelObjectMgr);

	// Compute each Planet distance to the observer
	const Vec3d obsHelioPos = core->getObserverHeliocentricEclipticPos();

	for (const auto& p : std::as_const(systemPlanets))
	{
		p->computeDistance(obsHelioPos);
	}

	// And sort them from the farthest to the closest. std::sort can split this into parallel threads!
	std::sort(STD_EXECUTION_PAR_COMMA
		  systemPlanets.begin(),systemPlanets.end(),biggerDistance());

	if (trailFader.getInterstate()>0.0000001f)
	{
		StelPainter sPainter(core->getProjection2d());
		const float ppx = static_cast<float>(sPainter.getProjector()->getDevicePixelsPerPixel());
		allTrails->setOpacity(trailFader.getInterstate());
		if (trailsThickness>1 || ppx>1.f)
			sPainter.setLineWidth(trailsThickness*ppx);
		allTrails->draw(core, &sPainter);
		if (trailsThickness>1 || ppx>1.f)
			sPainter.setLineWidth(1);
	}

	// Make some voodoo to determine when labels should be displayed
	const float sdLimitMag=static_cast<float>(core->getSkyDrawer()->getLimitMagnitude());
	const float maxMagLabel = (sdLimitMag<5.f ? sdLimitMag :
			5.f+(sdLimitMag-5.f)*1.2f) +(static_cast<float>(labelsAmount)-3.f)*1.2f;
	const double eclipseFactor=getSolarEclipseFactor(core).first;

	// Draw the elements
	for (const auto& p : std::as_const(systemPlanets))
	{
		if ( (p != sun) || (/* (p == sun) && */ !(core->getSkyDrawer()->getFlagDrawSunAfterAtmosphere())))
			p->draw(core, maxMagLabel, planetNameFont, eclipseFactor);
	}
	if (nbMarkers>0)
	{
		StelPainter sPainter(core->getProjection2d());
		postDrawAsteroidMarkers(&sPainter);
	}

	if (sObjMgr->getFlagSelectedObjectPointer() && getFlagPointer())
		drawPointer(core);
}

// Finalize the drawing of asteroid markers (inspired from StelSkyDrawer)
void SolarSystem::postDrawAsteroidMarkers(StelPainter *sPainter)
{
	Q_ASSERT(sPainter);

	if (nbMarkers==0)
		return;

	markerCircleTex->bind();
	sPainter->setBlending(true, GL_ONE, GL_ONE);

	const QMatrix4x4 qMat=sPainter->getProjector()->getProjectionMatrix().toQMatrix();

	vbo->bind();
	vbo->write(0, markerArray, nbMarkers*6*sizeof(MarkerVertex));
	vbo->write(maxMarkers*6*sizeof(MarkerVertex), textureCoordArray, nbMarkers*6*2);
	vbo->release();

	markerShaderProgram->bind();
	markerShaderProgram->setUniformValue(markerShaderVars.projectionMatrix, qMat);

	bindVAO();
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(nbMarkers)*6);
	releaseVAO();

	markerShaderProgram->release();

	nbMarkers = 0;
}

// Draw a point source halo.
bool SolarSystem::drawAsteroidMarker(StelCore* core, StelPainter* sPainter, const float x, const float y, Vec3f &color)
{
	const float reducer=markerFader.getInterstate();
	if (reducer==0.)
		return false;

	Q_ASSERT(sPainter);
	const float radius = 3.f * static_cast<float>(sPainter->getProjector()->getDevicePixelsPerPixel());
	unsigned char markerColor[3] = {
		static_cast<unsigned char>(std::min(static_cast<int>(color[0]*reducer*255+0.5f), 255)),
		static_cast<unsigned char>(std::min(static_cast<int>(color[1]*reducer*255+0.5f), 255)),
		static_cast<unsigned char>(std::min(static_cast<int>(color[2]*reducer*255+0.5f), 255))};
	// Store the drawing instructions in the vertex arrays
	MarkerVertex* vx = &(markerArray[nbMarkers*6]);
	vx->pos.set(x-radius,y-radius); std::memcpy(vx->color, markerColor, 3); ++vx;
	vx->pos.set(x+radius,y-radius); std::memcpy(vx->color, markerColor, 3); ++vx;
	vx->pos.set(x+radius,y+radius); std::memcpy(vx->color, markerColor, 3); ++vx;
	vx->pos.set(x-radius,y-radius); std::memcpy(vx->color, markerColor, 3); ++vx;
	vx->pos.set(x+radius,y+radius); std::memcpy(vx->color, markerColor, 3); ++vx;
	vx->pos.set(x-radius,y+radius); std::memcpy(vx->color, markerColor, 3); ++vx;

	++nbMarkers;
	if (nbMarkers>=maxMarkers)
	{
		// Flush the buffer (draw all buffered markers)
		postDrawAsteroidMarkers(sPainter);
	}
	return true;
}

void SolarSystem::drawEphemerisItems(const StelCore* core)
{
	if (flagShow || (!flagShow && getFlagEphemerisAlwaysOn()))
	{
		if (getFlagEphemerisMarkers())
			drawEphemerisMarkers(core);
		if (getFlagEphemerisLine())
			drawEphemerisLine(core);
	}
}

Vec3f SolarSystem::getEphemerisMarkerColor(int index) const
{
	// Sync index with AstroCalcDialog::generateEphemeris(). If required, switch to using a QMap.
	const QVector<Vec3f> colors={
		ephemerisGenericMarkerColor,
		ephemerisSecondaryMarkerColor,
		ephemerisMercuryMarkerColor,
		ephemerisVenusMarkerColor,
		ephemerisMarsMarkerColor,
		ephemerisJupiterMarkerColor,
		ephemerisSaturnMarkerColor};
	return colors.value(index, ephemerisGenericMarkerColor);
}

void SolarSystem::drawEphemerisMarkers(const StelCore *core)
{
	const int fsize = AstroCalcDialog::EphemerisList.count();
	if (fsize==0) return;

	StelProjectorP prj;
	if (getFlagEphemerisHorizontalCoordinates())
		prj = core->getProjection(StelCore::FrameAltAz, StelCore::RefractionOff);
	else
		prj = core->getProjection(StelCore::FrameJ2000);
	StelPainter sPainter(prj);

	float size, shift, baseSize = 4.f;
	const bool showDates = getFlagEphemerisDates();
	const bool showMagnitudes = getFlagEphemerisMagnitudes();
	const bool showSkippedData = getFlagEphemerisSkipData();
	const bool skipMarkers = getFlagEphemerisSkipMarkers();
	const bool isNowVisible = getFlagEphemerisNow();
	const int dataStep = getEphemerisDataStep();
	const int sizeCoeff = getEphemerisLineThickness() - 1;
	QString info = "";
	Vec3d win;
	Vec3f markerColor;

	if (getFlagEphemerisLine() && getFlagEphemerisScaleMarkers())
		baseSize = 3.f; // The line lies through center of marker

	if (isNowVisible)
	{
		const int limit = getEphemerisDataLimit();
		const int nsize = static_cast<int>(fsize/limit);
		sPainter.setBlending(true, GL_ONE, GL_ONE);
		texEphemerisNowMarker->bind();
		Vec3d pos;
		Vec3f win;
		for (int i =0; i < limit; i++)
		{
			long k = static_cast<long>(i)*nsize;
			sPainter.setColor(getEphemerisMarkerColor(AstroCalcDialog::EphemerisList[k].colorIndex));
			if (getFlagEphemerisHorizontalCoordinates())
				pos = AstroCalcDialog::EphemerisList[k].sso->getAltAzPosAuto(core);
			else
				pos = AstroCalcDialog::EphemerisList[k].sso->getJ2000EquatorialPos(core);
			if (prj->project(pos, win))
				sPainter.drawSprite2dMode(static_cast<float>(win[0]), static_cast<float>(win[1]), 6.f, 0.f);
		}
	}

	for (int i =0; i < fsize; i++)
	{
		bool skipFlag = (((i + 1)%dataStep)!=1 && dataStep!=1);

		// Check visibility of pointer
		if (!(sPainter.getProjector()->projectCheck(AstroCalcDialog::EphemerisList[i].coord, win)))
			continue;

		QString debugStr; // Used temporarily for development
		const bool isComet=AstroCalcDialog::EphemerisList[i].isComet;
		if (i == AstroCalcDialog::DisplayedPositionIndex)
		{
			markerColor = getEphemerisSelectedMarkerColor();
			size = 6.f;
		}
		else
		{
			markerColor = getEphemerisMarkerColor(AstroCalcDialog::EphemerisList[i].colorIndex);
			size = baseSize;
		}
		if (isComet) size += 16.f;
		size += sizeCoeff;
		sPainter.setColor(markerColor);
		sPainter.setBlending(true, GL_ONE, GL_ONE);
		if (isComet)
			texEphemerisCometMarker->bind();
		else
			texEphemerisMarker->bind();

		if (skipMarkers && skipFlag)
			continue;

		Vec3f win;
		if (prj->project(AstroCalcDialog::EphemerisList[i].coord, win))
		{
			float solarAngle=0.f; // Angle to possibly rotate the texture. Degrees.
			if (isComet)
			{
				// compute solarAngle in screen space.
				Vec3f sunWin;
				prj->project(AstroCalcDialog::EphemerisList[i].sunCoord, sunWin);
				// TODO: In some projections, we may need to test result and flip/mirror the angle, or deal with wrap-around effects.
				// E.g., in cylindrical mode, the comet icon will flip as soon as the corresponding sun position wraps around the screen edge.
				solarAngle=M_180_PIf*(atan2(-(win[1]-sunWin[1]), win[0]-sunWin[0]));
				// This will show projected positions and angles usable in labels.
				debugStr = QString("Sun: %1/%2 Obj: %3/%4 -->%5").arg(QString::number(sunWin[0]), QString::number(sunWin[1]), QString::number(win[0]), QString::number(win[1]), QString::number(solarAngle));
			}
			//sPainter.drawSprite2dMode(static_cast<float>(win[0]), static_cast<float>(win[1]), size, 180.f+AstroCalcDialog::EphemerisList[i].solarAngle*M_180_PIf);
			sPainter.drawSprite2dMode(static_cast<float>(win[0]), static_cast<float>(win[1]), size, 270.f-solarAngle);
		}

		if (showDates || showMagnitudes)
		{
			if (showSkippedData && skipFlag)
				continue;

			shift = 3.f + size/1.6f;
			if (showDates && showMagnitudes)
				info = QString("%1 (%2)").arg(AstroCalcDialog::EphemerisList[i].objDateStr, QString::number(AstroCalcDialog::EphemerisList[i].magnitude, 'f', 2));
			if (showDates && !showMagnitudes)
				info = AstroCalcDialog::EphemerisList[i].objDateStr;
			if (!showDates && showMagnitudes)
				info = QString::number(AstroCalcDialog::EphemerisList[i].magnitude, 'f', 2);

			// Activate for debug labels.
			//info=debugStr;
			sPainter.drawText(AstroCalcDialog::EphemerisList[i].coord, info, 0, shift, shift, false);
		}
	}
}

void SolarSystem::drawEphemerisLine(const StelCore *core)
{
	const int size = AstroCalcDialog::EphemerisList.count();
	if (size==0) return;

	// The array of data is not empty - good news!
	StelProjectorP prj;
	if (getFlagEphemerisHorizontalCoordinates())
		prj = core->getProjection(StelCore::FrameAltAz, StelCore::RefractionOff);
	else
		prj = core->getProjection(StelCore::FrameJ2000);
	StelPainter sPainter(prj);
	const float ppx = static_cast<float>(sPainter.getProjector()->getDevicePixelsPerPixel());

	const float oldLineThickness=sPainter.getLineWidth();
	const float lineThickness = getEphemerisLineThickness()*ppx;
	if (!fuzzyEquals(lineThickness, oldLineThickness))
		sPainter.setLineWidth(lineThickness);

	Vec3f color;
	QVector<Vec3d> vertexArray;
	QVector<Vec4f> colorArray;
	const int limit = getEphemerisDataLimit();
	const int nsize = static_cast<int>(size/limit);
	vertexArray.resize(nsize);
	colorArray.resize(nsize);
	for (int j=0; j<limit; j++)
	{
		for (int i =0; i < nsize; i++)
		{
			color = getEphemerisMarkerColor(AstroCalcDialog::EphemerisList[i + j*nsize].colorIndex);
			colorArray[i]=Vec4f(color, 1.0f);
			vertexArray[i]=AstroCalcDialog::EphemerisList[i + j*nsize].coord;
		}
		sPainter.drawPath(vertexArray, colorArray);
	}

	if (!fuzzyEquals(lineThickness, oldLineThickness))
		sPainter.setLineWidth(oldLineThickness); // restore line thickness
}

void SolarSystem::fillEphemerisDates()
{
	const int fsize = AstroCalcDialog::EphemerisList.count();
	if (fsize==0) return;

	static StelLocaleMgr* localeMgr = &StelApp::getInstance().getLocaleMgr();
	static StelCore *core = StelApp::getInstance().getCore();
	const bool showSmartDates = getFlagEphemerisSmartDates();
	double JD = AstroCalcDialog::EphemerisList.first().objDate;
	bool withTime = (fsize>1 && (AstroCalcDialog::EphemerisList[1].objDate-JD<1.0));

	int fYear, fMonth, fDay, sYear, sMonth, sDay, h, m, s;
	QString info;
	const double shift = core->getUTCOffset(JD)*StelCore::JD_HOUR;
	StelUtils::getDateFromJulianDay(JD+shift, &fYear, &fMonth, &fDay);
	bool sFlag = true;
	sYear = fYear;
	sMonth = fMonth;
	sDay = fDay;
	const bool showSkippedData = getFlagEphemerisSkipData();
	const int dataStep = getEphemerisDataStep();

	for (int i = 0; i < fsize; i++)
	{
		const double JD = AstroCalcDialog::EphemerisList[i].objDate;
		StelUtils::getDateFromJulianDay(JD+shift, &fYear, &fMonth, &fDay);

		if (showSkippedData && ((i + 1)%dataStep)!=1 && dataStep!=1)
			continue;

		if (showSmartDates)
		{
			if (sFlag)
				info = QString("%1").arg(fYear);

			if (info.isEmpty() && !sFlag && fYear!=sYear)
				info = QString("%1").arg(fYear);

			if (!info.isEmpty())
				info.append(QString("/%1").arg(localeMgr->romanMonthName(fMonth)));
			else if (fMonth!=sMonth)
				info = QString("%1").arg(localeMgr->romanMonthName(fMonth));

			if (!info.isEmpty())
				info.append(QString("/%1").arg(fDay));
			else
				info = QString("%1").arg(fDay);

			if (withTime) // very short step
			{
				if (fDay==sDay && !sFlag)
					info.clear();

				StelUtils::getTimeFromJulianDay(JD+shift, &h, &m, &s);
				QString hours = QString::number(h).rightJustified(2, '0');
				QString minutes = QString::number(m).rightJustified(2, '0');
				if (!info.isEmpty())
					info.append(QString(" %1:%2").arg(hours, minutes));
				else
					info = QString("%1:%2").arg(hours, minutes);
			}

			AstroCalcDialog::EphemerisList[i].objDateStr = info;
			info.clear();
			sYear = fYear;
			sMonth = fMonth;
			sDay = fDay;
			sFlag = false;
		}
		else
		{
			// OK, let's use standard formats for date and time (as defined for whole planetarium)
			const double utcOffsetHrs = core->getUTCOffset(JD);
			if (withTime)
				AstroCalcDialog::EphemerisList[i].objDateStr = QString("%1 %2").arg(localeMgr->getPrintableDateLocal(JD, utcOffsetHrs), localeMgr->getPrintableTimeLocal(JD, utcOffsetHrs));
			else
				AstroCalcDialog::EphemerisList[i].objDateStr = localeMgr->getPrintableDateLocal(JD, utcOffsetHrs);
		}
	}
}

PlanetP SolarSystem::searchByEnglishName(const QString &planetEnglishName) const
{
	const QString planetEnglishNameUpper=planetEnglishName.toUpper();
	for (const auto& p : systemPlanets)
	{
		if (p->getEnglishName().toUpper() == planetEnglishNameUpper)
			return p;

		// IAU designation?
		QString iau = p->getIAUDesignation();
		if (!iau.isEmpty() && iau.toUpper()==planetEnglishNameUpper)
			return p;
	}
	for (const auto& p : systemMinorBodies)
	{
		QStringList c;
		// other comet designations?
		if (p->getPlanetType()==Planet::isComet)
		{
			QSharedPointer<Comet> mp = p.dynamicCast<Comet>();
			c = mp->getExtraDesignations();
		} else {
			QSharedPointer<MinorPlanet> mp = p.dynamicCast<MinorPlanet>();
			c = mp->getExtraDesignations();
		}
		for (const auto& d : std::as_const(c))
		{
			if (d.toUpper()==planetEnglishNameUpper)
				return p;
		}
	}
	return PlanetP();
}

PlanetP SolarSystem::searchMinorPlanetByEnglishName(const QString &planetEnglishName) const
{
	const QString planetEnglishNameUpper=planetEnglishName.toUpper();
	for (const auto& p : systemMinorBodies)
	{
		if (p->getEnglishName().toUpper() == planetEnglishNameUpper)
			return p;

		// IAU designation?
		QString iau = p->getIAUDesignation();
		if (!iau.isEmpty() && iau.toUpper()==planetEnglishNameUpper)
			return p;

		QStringList c;
		// other comet designations?
		if (p->getPlanetType()==Planet::isComet)
		{
			QSharedPointer<Comet> mp = p.dynamicCast<Comet>();
			c = mp->getExtraDesignations();
		}
		else
		{
			QSharedPointer<MinorPlanet> mp = p.dynamicCast<MinorPlanet>();
			c = mp->getExtraDesignations();
		}
		for (const auto& d : std::as_const(c))
		{
			if (d.toUpper()==planetEnglishNameUpper)
				return p;
		}
	}
	return PlanetP();
}


StelObjectP SolarSystem::searchByNameI18n(const QString& planetNameI18n) const
{
	const QString planetNameI18Upper=planetNameI18n.toUpper();
	for (const auto& p : systemPlanets)
	{
		QString nativeNameI18nUpper = p->getNameNativeI18n().toUpper();
		if (p->getNameI18n().toUpper() == planetNameI18Upper || (!nativeNameI18nUpper.isEmpty() && nativeNameI18nUpper == planetNameI18Upper))
			return qSharedPointerCast<StelObject>(p);
	}
	return StelObjectP();
}


StelObjectP SolarSystem::searchByName(const QString& name) const
{
	const QString nameUpper=name.toUpper();
	for (const auto& p : systemPlanets)
	{
		QString nativeName = p->getNameNative().toUpper();
		if (p->getEnglishName().toUpper() == nameUpper || (!nativeName.isEmpty() && nativeName == nameUpper))
			return qSharedPointerCast<StelObject>(p);

		// IAU designation?
		QString iau = p->getIAUDesignation();
		if (!iau.isEmpty() && iau.toUpper()==nameUpper)
			return qSharedPointerCast<StelObject>(p);
	}
	for (const auto& p : systemMinorBodies)
	{
		QStringList c;
		// other comet designations?
		if (p->getPlanetType()==Planet::isComet)
		{
			QSharedPointer<Comet> mp = p.dynamicCast<Comet>();
			c = mp->getExtraDesignations();
		}
		else
		{
			QSharedPointer<MinorPlanet> mp = p.dynamicCast<MinorPlanet>();
			c = mp->getExtraDesignations();
		}
		for (const auto& d : std::as_const(c))
		{
			if (d.toUpper()==nameUpper)
				return qSharedPointerCast<StelObject>(p);
		}
	}

	return StelObjectP();
}

float SolarSystem::getPlanetVMagnitude(const QString &planetName, bool withExtinction) const
{
	StelCore *core=StelApp::getInstance().getCore();
	double eclipseFactor=getSolarEclipseFactor(core).first;
	PlanetP p = searchByEnglishName(planetName);
	if (p.isNull()) // Possible was asked the common name of minor planet?
		p = searchMinorPlanetByEnglishName(planetName);
	float r = p->getVMagnitude(core, eclipseFactor);
	if (withExtinction)
		r = p->getVMagnitudeWithExtinction(core, r);
	return r;
}

QString SolarSystem::getPlanetType(const QString &planetName) const
{
	PlanetP p = searchByEnglishName(planetName);
	if (p.isNull()) // Possible was asked the common name of minor planet?
		p = searchMinorPlanetByEnglishName(planetName);
	if (p.isNull())
		return QString("UNDEFINED");
	return p->getObjectType();
}

double SolarSystem::getDistanceToPlanet(const QString &planetName) const
{
	PlanetP p = searchByEnglishName(planetName);
	if (p.isNull()) // Possible was asked the common name of minor planet?
		p = searchMinorPlanetByEnglishName(planetName);
	return p->getDistance();
}

double SolarSystem::getElongationForPlanet(const QString &planetName) const
{
	PlanetP p = searchByEnglishName(planetName);
	if (p.isNull()) // Possible was asked the common name of minor planet?
		p = searchMinorPlanetByEnglishName(planetName);
	return p->getElongation(StelApp::getInstance().getCore()->getObserverHeliocentricEclipticPos());
}

double SolarSystem::getPhaseAngleForPlanet(const QString &planetName) const
{
	PlanetP p = searchByEnglishName(planetName);
	if (p.isNull()) // Possible was asked the common name of minor planet?
		p = searchMinorPlanetByEnglishName(planetName);
	return p->getPhaseAngle(StelApp::getInstance().getCore()->getObserverHeliocentricEclipticPos());
}

float SolarSystem::getPhaseForPlanet(const QString &planetName) const
{
	PlanetP p = searchByEnglishName(planetName);
	if (p.isNull()) // Possible was asked the common name of minor planet?
		p = searchMinorPlanetByEnglishName(planetName);
	return p->getPhase(StelApp::getInstance().getCore()->getObserverHeliocentricEclipticPos());
}

QStringList SolarSystem::getObjectsList(QString objType) const
{
	QStringList r;
	if (objType.toLower()==L1S("all"))
	{
		r = listAllObjects(true);
		// Remove the Sun
		r.removeOne("Sun");
		// Remove special objects
		r.removeOne("Solar System Observer");
		r.removeOne("Earth Observer");
		r.removeOne("Mars Observer");
		r.removeOne("Jupiter Observer");
		r.removeOne("Saturn Observer");
		r.removeOne("Uranus Observer");
		r.removeOne("Neptune Observer");
	}
	else
		r = listAllObjectsByType(objType, true);

	return r;
}

// Search if any Planet is close to position given in earth equatorial position and return the distance
StelObjectP SolarSystem::search(Vec3d pos, const StelCore* core) const
{
	pos.normalize();
	PlanetP closest;
	double cos_angle_closest = 0.;
	Vec3d equPos;

	for (const auto& p : systemPlanets)
	{
		equPos = p->getEquinoxEquatorialPos(core);
		equPos.normalize();
		double cos_ang_dist = equPos*pos;
		if (cos_ang_dist>cos_angle_closest)
		{
			closest = p;
			cos_angle_closest = cos_ang_dist;
		}
	}

	if (cos_angle_closest>0.999)
	{
		return qSharedPointerCast<StelObject>(closest);
	}
	else return StelObjectP();
}

// Return a QList containing the planets located inside the limFov circle around position vv
QList<StelObjectP> SolarSystem::searchAround(const Vec3d& vv, double limitFov, const StelCore* core) const
{
	QList<StelObjectP> result;
	if (!getFlagPlanets())
		return result;

	Vec3d v(vv);
	
	double cosLimFov = std::cos(limitFov * M_PI/180.);
	Vec3d equPos;

	const QString weAreHere = core->getCurrentPlanet()->getEnglishName();
	for (const auto& p : systemPlanets)
	{
		equPos = p->getJ2000EquatorialPos(core);
		equPos.normalize();

		double cosAngularSize = std::cos(p->getSpheroidAngularRadius(core) * M_PI/180.);

		if (equPos*v>=std::min(cosLimFov, cosAngularSize) && p->getEnglishName()!=weAreHere)
		{
			result.append(qSharedPointerCast<StelObject>(p));
		}
	}
	return result;
}

// Update i18 names from english names according to current sky culture translator
void SolarSystem::updateI18n()
{
	const StelTranslator& trans = StelApp::getInstance().getLocaleMgr().getSkyTranslator();
	for (const auto& p : std::as_const(systemPlanets))
		p->translateName(trans);
}

QStringList SolarSystem::listMatchingObjects(const QString& objPrefix, int maxNbItem, bool useStartOfWords) const
{
	QStringList result;
	if (getFlagPlanets())
		result = StelObjectModule::listMatchingObjects(objPrefix, maxNbItem, useStartOfWords);
	return result;
}

void SolarSystem::setFlagTrails(bool b)
{
	if (getFlagTrails() != b)
	{
		trailFader = b;
		if (b)
		{
			allTrails->reset(maxTrailPoints);
			recreateTrails();
		}
		StelApp::immediateSave("astro/flag_object_trails", b);
		emit trailsDisplayedChanged(b);
	}
}

bool SolarSystem::getFlagTrails() const
{
	return static_cast<bool>(trailFader);
}

void SolarSystem::setMaxTrailPoints(int max)
{
	if (maxTrailPoints != max)
	{
		maxTrailPoints = max;
		allTrails->reset(max);
		recreateTrails();
		StelApp::immediateSave("viewing/max_trail_points", max);
		emit maxTrailPointsChanged(max);
	}
}

void SolarSystem::setMaxTrailTimeExtent(int max)
{
	if (maxTrailTimeExtent != max && maxTrailTimeExtent > 0)
	{
		maxTrailTimeExtent = max;
		recreateTrails();
		StelApp::immediateSave("viewing/max_trail_time_extent", max);
		emit maxTrailTimeExtentChanged(max);
	}
}

void SolarSystem::setTrailsThickness(int v)
{
	if (trailsThickness != v)
	{
		trailsThickness = v;
		StelApp::immediateSave("astro/object_trails_thickness", v);
		emit trailsThicknessChanged(v);
	}
}

void SolarSystem::setFlagHints(bool b)
{
	if (getFlagHints() != b)
	{
		for (const auto& p : std::as_const(systemPlanets))
			p->setFlagHints(b);
		StelApp::immediateSave("astro/flag_planets_hints", b);
		emit flagHintsChanged(b);
	}
}

bool SolarSystem::getFlagHints(void) const
{
	for (const auto& p : std::as_const(systemPlanets))
	{
		if (p->getFlagHints())
			return true;
	}
	return false;
}

void SolarSystem::setFlagLabels(bool b)
{
	if (getFlagLabels() != b)
	{
		for (const auto& p : std::as_const(systemPlanets))
			p->setFlagLabels(b);
		StelApp::immediateSave("astro/flag_planets_labels", b);
		emit labelsDisplayedChanged(b);
	}
}

bool SolarSystem::getFlagLabels() const
{
	for (const auto& p : std::as_const(systemPlanets))
	{
		if (p->getFlagLabels())
			return true;
	}
	return false;
}

void SolarSystem::setFlagMarkers(bool b)
{
	if (getFlagMarkers() != b)
	{
		markerFader = b;
		StelApp::immediateSave("astro/flag_planets_markers", b);
		emit markersDisplayedChanged(b);
	}
}

bool SolarSystem::getFlagMarkers() const
{
	return markerFader;
}

void SolarSystem::setFlagLightTravelTime(bool b)
{
	if(b!=flagLightTravelTime)
	{
		flagLightTravelTime = b;
		StelApp::immediateSave("astro/flag_light_travel_time", b);
		emit flagLightTravelTimeChanged(b);
	}
}

void SolarSystem::setFlagShowObjSelfShadows(bool b)
{
	if(b!=flagShowObjSelfShadows)
	{
		flagShowObjSelfShadows = b;
		if(!b)
			Planet::deinitFBO();
		StelApp::immediateSave("astro/flag_show_obj_self_shadows", b);
		emit flagShowObjSelfShadowsChanged(b);
	}
}

void SolarSystem::setSelected(PlanetP obj)
{
	if (obj && obj->getType() == L1S("Planet"))
	{
		selected = obj;
		selectedSSO.push_back(obj);
	}
	else
		selected.clear();
	// Undraw other objects hints, orbit, trails etc..
	setFlagHints(getFlagHints());
	reconfigureOrbits();
}


void SolarSystem::update(double deltaTime)
{
	trailFader.update(static_cast<int>(deltaTime*1000));
	if (trailFader.getInterstate()>0.f)
	{
		allTrails->update();
	}

	for (const auto& p : std::as_const(systemPlanets))
	{
		p->update(static_cast<int>(deltaTime*1000));
	}
	markerFader.update(deltaTime*1000);
}

// is a lunar eclipse close at hand?
bool SolarSystem::nearLunarEclipse() const
{
	// TODO: could replace with simpler test
	// TODO Source?

	const Vec3d sun = getSun()->getAberrationPush();
	const Vec3d e = getEarth()->getEclipticPos();
	const Vec3d m = getMoon()->getEclipticPos();  // relative to earth
	const Vec3d mh = getMoon()->getHeliocentricEclipticPos();  // relative to sun

	// shadow location at earth + moon distance along earth vector from (aberrated) sun
	Vec3d en = e-sun;
	en.normalize();
	Vec3d shadow = en * (e.norm() + m.norm());

	// find shadow radii in AU
	double r_penumbra = shadow.norm()*702378.1/AU/e.norm() - SUN_RADIUS/AU;

	// modify shadow location for scaled moon
	Vec3d mdist = shadow - mh;
	if(mdist.norm() > r_penumbra + 2000./AU) return false;   // not visible so don't bother drawing

	return true;
}

QStringList SolarSystem::listAllObjects(bool inEnglish) const
{
	QStringList result;
	if (inEnglish)
	{
		for (const auto& p : systemPlanets)
		{
			result << p->getEnglishName();
			if (!p->getIAUDesignation().isEmpty())
				result << p->getIAUDesignation();
		}
	}
	else
	{
		for (const auto& p : systemPlanets)
		{
			result << p->getNameI18n();
			if (!p->getNameNativeI18n().isEmpty())
				result << p->getNameNativeI18n() << p->getNameNative();
			if (!p->getIAUDesignation().isEmpty())
				result << p->getIAUDesignation();
		}
	}
	for (const auto& p : systemMinorBodies)
	{
		QStringList c;
		// other comet designations?
		if (p->getPlanetType()==Planet::isComet)
		{
			QSharedPointer<Comet> mp = p.dynamicCast<Comet>();
			c = mp->getExtraDesignations();
		} else {
			QSharedPointer<MinorPlanet> mp = p.dynamicCast<MinorPlanet>();
			c = mp->getExtraDesignations();
		}
		if (c.count()>0)
			result << c;
	}
	return result;
}

QStringList SolarSystem::listAllObjectsByType(const QString &objType, bool inEnglish) const
{
	QStringList result;
	if (inEnglish)
	{
		for (const auto& p : systemPlanets)
		{
			if (p->getObjectType()==objType)
			{
				result << p->getEnglishName();
				if (!p->getIAUDesignation().isEmpty())
					result << p->getIAUDesignation();
			}
		}
	}
	else
	{
		for (const auto& p : systemPlanets)
		{
			if (p->getObjectType()==objType)
			{
				result << p->getNameI18n();
				if (!p->getIAUDesignation().isEmpty())
					result << p->getIAUDesignation();
			}
		}
	}
	for (const auto& p : systemMinorBodies)
	{
		if (p->getObjectType()==objType)
		{
			QStringList c;
			// other comet designations?
			if (p->getPlanetType()==Planet::isComet)
			{
				QSharedPointer<Comet> mp = p.dynamicCast<Comet>();
				c = mp->getExtraDesignations();
			} else {
				QSharedPointer<MinorPlanet> mp = p.dynamicCast<MinorPlanet>();
				c = mp->getExtraDesignations();
			}
			if (c.count()>0)
				result << c;
		}
	}
	return result;
}

void SolarSystem::selectedObjectChange(StelModule::StelModuleSelectAction)
{
	const QList<StelObjectP> newSelected = GETSTELMODULE(StelObjectMgr)->getSelectedObject("Planet");
	if (!newSelected.empty())
	{
		setSelected(qSharedPointerCast<Planet>(newSelected[0]));
		if (getFlagIsolatedTrails())
			recreateTrails();
	}
	else
		setSelected("");
}

// Activate/Deactivate planets display
void SolarSystem::setFlagPlanets(bool b)
{
	if (b!=flagShow)
	{
		flagShow=b;
		StelApp::immediateSave("astro/flag_planets", b);
		emit flagPlanetsDisplayedChanged(b);
	}
}

bool SolarSystem::getFlagPlanets(void) const
{
	return flagShow;
}

void SolarSystem::setFlagEphemerisMarkers(bool b)
{
	if (b!=ephemerisMarkersDisplayed)
	{
		ephemerisMarkersDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_markers", b); // Immediate saving of state
		emit ephemerisMarkersChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisMarkers() const
{
	return ephemerisMarkersDisplayed;
}

void SolarSystem::setFlagEphemerisLine(bool b)
{
	if (b!=ephemerisLineDisplayed)
	{
		ephemerisLineDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_line", b); // Immediate saving of state
		emit ephemerisLineChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisLine() const
{
	return ephemerisLineDisplayed;
}

bool SolarSystem::getFlagEphemerisAlwaysOn() const
{
	return ephemerisAlwaysOn;
}

void SolarSystem::setFlagEphemerisAlwaysOn(bool b)
{
	if (b != ephemerisAlwaysOn)
	{
		ephemerisAlwaysOn = b;
		conf->setValue("astrocalc/flag_ephemeris_alwayson", b); // Immediate saving of state
		emit ephemerisAlwaysOnChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisNow() const
{
	return ephemerisNow;
}

void SolarSystem::setFlagEphemerisNow(bool b)
{
	if (b != ephemerisNow)
	{
		ephemerisNow = b;
		conf->setValue("astrocalc/flag_ephemeris_now", b); // Immediate saving of state
		emit ephemerisNowChanged(b);
	}
}

void SolarSystem::setFlagEphemerisHorizontalCoordinates(bool b)
{
	if (b!=ephemerisHorizontalCoordinates)
	{
		ephemerisHorizontalCoordinates=b;
		conf->setValue("astrocalc/flag_ephemeris_horizontal", b); // Immediate saving of state
		emit ephemerisHorizontalCoordinatesChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisHorizontalCoordinates() const
{
	return ephemerisHorizontalCoordinates;
}

void SolarSystem::setFlagEphemerisDates(bool b)
{
	if (b!=ephemerisDatesDisplayed)
	{
		ephemerisDatesDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_dates", b); // Immediate saving of state
		emit ephemerisDatesChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisDates() const
{
	return ephemerisDatesDisplayed;
}

void SolarSystem::setFlagEphemerisMagnitudes(bool b)
{
	if (b!=ephemerisMagnitudesDisplayed)
	{
		ephemerisMagnitudesDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_magnitudes", b); // Immediate saving of state
		emit ephemerisMagnitudesChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisMagnitudes() const
{
	return ephemerisMagnitudesDisplayed;
}

void SolarSystem::setFlagEphemerisSkipData(bool b)
{
	if (b!=ephemerisSkipDataDisplayed)
	{
		ephemerisSkipDataDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_skip_data", b); // Immediate saving of state
		emit ephemerisSkipDataChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisSkipData() const
{
	return ephemerisSkipDataDisplayed;
}

void SolarSystem::setFlagEphemerisSkipMarkers(bool b)
{
	if (b!=ephemerisSkipMarkersDisplayed)
	{
		ephemerisSkipMarkersDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_skip_markers", b); // Immediate saving of state
		emit ephemerisSkipMarkersChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisSkipMarkers() const
{
	return ephemerisSkipMarkersDisplayed;
}

void SolarSystem::setFlagEphemerisSmartDates(bool b)
{
	if (b!=ephemerisSmartDatesDisplayed)
	{
		ephemerisSmartDatesDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_smart_dates", b); // Immediate saving of state
		emit ephemerisSmartDatesChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisSmartDates() const
{
	return ephemerisSmartDatesDisplayed;
}

void SolarSystem::setFlagEphemerisScaleMarkers(bool b)
{
	if (b!=ephemerisScaleMarkersDisplayed)
	{
		ephemerisScaleMarkersDisplayed=b;
		conf->setValue("astrocalc/flag_ephemeris_scale_markers", b); // Immediate saving of state
		emit ephemerisScaleMarkersChanged(b);
	}
}

bool SolarSystem::getFlagEphemerisScaleMarkers() const
{
	return ephemerisScaleMarkersDisplayed;
}

void SolarSystem::setEphemerisDataStep(int step)
{
	ephemerisDataStep = step;
	// automatic saving of the setting
	conf->setValue("astrocalc/ephemeris_data_step", step);
	emit ephemerisDataStepChanged(step);
}

int SolarSystem::getEphemerisDataStep() const
{
	return ephemerisDataStep;
}

void SolarSystem::setEphemerisDataLimit(int limit)
{
	ephemerisDataLimit = limit;
	emit ephemerisDataLimitChanged(limit);
}

int SolarSystem::getEphemerisDataLimit() const
{
	return ephemerisDataLimit;
}

void SolarSystem::setEphemerisLineThickness(int v)
{
	ephemerisLineThickness = v;
	// automatic saving of the setting
	conf->setValue("astrocalc/ephemeris_line_thickness", v);
	emit ephemerisLineThicknessChanged(v);
}

int SolarSystem::getEphemerisLineThickness() const
{
	return ephemerisLineThickness;
}

void SolarSystem::setEphemerisGenericMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisGenericMarkerColor)
	{
		ephemerisGenericMarkerColor = color;
		emit ephemerisGenericMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisGenericMarkerColor() const
{
	return ephemerisGenericMarkerColor;
}

void SolarSystem::setEphemerisSecondaryMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisSecondaryMarkerColor)
	{
		ephemerisSecondaryMarkerColor = color;
		emit ephemerisSecondaryMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisSecondaryMarkerColor() const
{
	return ephemerisSecondaryMarkerColor;
}

void SolarSystem::setEphemerisSelectedMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisSelectedMarkerColor)
	{
		ephemerisSelectedMarkerColor = color;
		emit ephemerisSelectedMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisSelectedMarkerColor() const
{
	return ephemerisSelectedMarkerColor;
}

void SolarSystem::setEphemerisMercuryMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisMercuryMarkerColor)
	{
		ephemerisMercuryMarkerColor = color;
		emit ephemerisMercuryMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisMercuryMarkerColor() const
{
	return ephemerisMercuryMarkerColor;
}

void SolarSystem::setEphemerisVenusMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisVenusMarkerColor)
	{
		ephemerisVenusMarkerColor = color;
		emit ephemerisVenusMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisVenusMarkerColor() const
{
	return ephemerisVenusMarkerColor;
}

void SolarSystem::setEphemerisMarsMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisMarsMarkerColor)
	{
		ephemerisMarsMarkerColor = color;
		emit ephemerisMarsMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisMarsMarkerColor() const
{
	return ephemerisMarsMarkerColor;
}

void SolarSystem::setEphemerisJupiterMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisJupiterMarkerColor)
	{
		ephemerisJupiterMarkerColor = color;
		emit ephemerisJupiterMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisJupiterMarkerColor() const
{
	return ephemerisJupiterMarkerColor;
}

void SolarSystem::setEphemerisSaturnMarkerColor(const Vec3f& color)
{
	if (color!=ephemerisSaturnMarkerColor)
	{
		ephemerisSaturnMarkerColor = color;
		emit ephemerisSaturnMarkerColorChanged(color);
	}
}

Vec3f SolarSystem::getEphemerisSaturnMarkerColor() const
{
	return ephemerisSaturnMarkerColor;
}

void SolarSystem::setFlagIsolatedTrails(bool b)
{
	if(b!=flagIsolatedTrails)
	{
		flagIsolatedTrails = b;
		recreateTrails();
		StelApp::immediateSave("viewing/flag_isolated_trails", b);
		emit flagIsolatedTrailsChanged(b);
	}
}

bool SolarSystem::getFlagIsolatedTrails() const
{
	return flagIsolatedTrails;
}

int SolarSystem::getNumberIsolatedTrails() const
{
	return numberIsolatedTrails;
}

void SolarSystem::setNumberIsolatedTrails(int n)
{
	// [1..5] - valid range for trails
	numberIsolatedTrails = qBound(1, n, 5);

	if (getFlagIsolatedTrails())
		recreateTrails();

	StelApp::immediateSave("viewing/number_isolated_trails", n);
	emit numberIsolatedTrailsChanged(numberIsolatedTrails);
}

void SolarSystem::setFlagOrbits(bool b)
{
	if(b!=getFlagOrbits())
	{
		flagOrbits = b;
		StelApp::immediateSave("astro/flag_planets_orbits", b);
		emit flagOrbitsChanged(b);
	}
}

// Connect this to all signals when orbit selection or selected object has changed.
// This method goes through all planets and sets orbit drawing as configured by several flags
void SolarSystem::reconfigureOrbits()
{
	// State of before 24.3: You could display planet orbits only, selected object's orbit, but not mix selected minor body in relation to all planets.
	// The first
	// we have: flagOrbits O, flagIsolatedOrbits I, flagPlanetsOrbitsOnly P, flagOrbitsWithMoons M, flagPermanentOrbits and a possibly selected planet S
	// permanentOrbits only influences local drawing of a single planet and can be ignored here.
	// O S I P M
	// 0 X X X X  NONE
	// 1 0 1 X X  NONE
	// 1 X 0 0 X  ALL
	// 1 X 0 1 0  all planets only
	// 1 X 0 1 1  all planets with their moons only

	// 1 1 1 0 0  only selected SSO
	// 1 1 1 0 1  only selected SSO and orbits of its moon system
	// 1 1 1 1 0  only selected SSO if it is a major planet
	// 1 1 1 1 1  only selected SSO if it is a major planet, plus its system of moons

	if (!flagOrbits || (flagIsolatedOrbits && (!selected || selected==sun)))
	{
		for (const auto& p : std::as_const(systemPlanets))
			p->setFlagOrbits(false);
	}
	// from here, flagOrbits is certainly on
	else if (!flagIsolatedOrbits)
	{
		for (const auto& p : std::as_const(systemPlanets))
			p->setFlagOrbits(!flagPlanetsOrbitsOnly || (p->getPlanetType()==Planet::isPlanet || (flagOrbitsWithMoons && p->parent && p->parent->getPlanetType()==Planet::isPlanet) ));
	}
	else // flagIsolatedOrbits && selected
	{
		// Display only orbit for selected planet and, if requested, its moons.
		for (const auto& p : std::as_const(systemPlanets))
			p->setFlagOrbits(   (p==selected && (  !flagPlanetsOrbitsOnly ||  p->getPlanetType()==Planet::isPlanet ) )
					 || (flagOrbitsWithMoons && p->getPlanetType()==Planet::isMoon && p->parent==selected ) );
	}
	// 24.3: With new flag, we can override to see the orbits of major planets together with that of a single selected minor body.
	if (flagOrbits && flagPlanetsOrbits)
	{
		for (const auto& p : std::as_const(systemPlanets))
			if ((p->getPlanetType()==Planet::isPlanet) || (flagOrbitsWithMoons && p->getPlanetType()==Planet::isMoon ))
				p->setFlagOrbits(true);
	}
}

void SolarSystem::setFlagIsolatedOrbits(bool b)
{
	if(b!=flagIsolatedOrbits)
	{
		flagIsolatedOrbits = b;
		StelApp::immediateSave("viewing/flag_isolated_orbits", b);
		emit flagIsolatedOrbitsChanged(b);
	}
}

bool SolarSystem::getFlagIsolatedOrbits() const
{
	return flagIsolatedOrbits;
}

void SolarSystem::setFlagPlanetsOrbits(bool b)
{
	if(b!=flagPlanetsOrbits)
	{
		flagPlanetsOrbits = b;
		StelApp::immediateSave("viewing/flag_planets_orbits", b);
		emit flagPlanetsOrbitsChanged(b);
	}
}

bool SolarSystem::getFlagPlanetsOrbits() const
{
	return flagPlanetsOrbits;
}

void SolarSystem::setFlagPlanetsOrbitsOnly(bool b)
{
	if(b!=flagPlanetsOrbitsOnly)
	{
		flagPlanetsOrbitsOnly = b;
		StelApp::immediateSave("viewing/flag_planets_orbits_only", b);
		emit flagPlanetsOrbitsOnlyChanged(b);
	}
}

bool SolarSystem::getFlagPlanetsOrbitsOnly() const
{
	return flagPlanetsOrbitsOnly;
}

void SolarSystem::setFlagOrbitsWithMoons(bool b)
{
	if(b!=flagOrbitsWithMoons)
	{
		flagOrbitsWithMoons = b;
		StelApp::immediateSave("viewing/flag_orbits_with_moons", b);
		emit flagOrbitsWithMoonsChanged(b);
	}
}

bool SolarSystem::getFlagOrbitsWithMoons() const
{
	return flagOrbitsWithMoons;
}

// Set/Get planets names color
void SolarSystem::setLabelsColor(const Vec3f& c)
{
	if (c!=Planet::getLabelColor())
	{
		Planet::setLabelColor(c);
		StelApp::immediateSave("color/planet_names_color", c.toStr());
		emit labelsColorChanged(c);
	}
}

Vec3f SolarSystem::getLabelsColor(void) const
{
	return Planet::getLabelColor();
}

// Set/Get orbits lines color
void SolarSystem::setOrbitsColor(const Vec3f& c)
{
	if (c!=Planet::getOrbitColor())
	{
		Planet::setOrbitColor(c);
		StelApp::immediateSave("color/sso_orbits_color", c.toStr());
		emit orbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getOrbitsColor(void) const
{
	return Planet::getOrbitColor();
}

void SolarSystem::setMajorPlanetsOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getMajorPlanetOrbitColor())
	{
		Planet::setMajorPlanetOrbitColor(c);
		StelApp::immediateSave("color/major_planets_orbits_color", c.toStr());
		emit majorPlanetsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getMajorPlanetsOrbitsColor(void) const
{
	return Planet::getMajorPlanetOrbitColor();
}

void SolarSystem::setMinorPlanetsOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getMinorPlanetOrbitColor())
	{
		Planet::setMinorPlanetOrbitColor(c);
		StelApp::immediateSave("color/minor_planets_orbits_color", c.toStr());
		emit minorPlanetsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getMinorPlanetsOrbitsColor(void) const
{
	return Planet::getMinorPlanetOrbitColor();
}

void SolarSystem::setDwarfPlanetsOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getDwarfPlanetOrbitColor())
	{
		Planet::setDwarfPlanetOrbitColor(c);
		StelApp::immediateSave("color/dwarf_planets_orbits_color", c.toStr());
		emit dwarfPlanetsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getDwarfPlanetsOrbitsColor(void) const
{
	return Planet::getDwarfPlanetOrbitColor();
}

void SolarSystem::setMoonsOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getMoonOrbitColor())
	{
		Planet::setMoonOrbitColor(c);
		StelApp::immediateSave("color/moon_orbits_color", c.toStr());
		emit moonsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getMoonsOrbitsColor(void) const
{
	return Planet::getMoonOrbitColor();
}

void SolarSystem::setCubewanosOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getCubewanoOrbitColor())
	{
		Planet::setCubewanoOrbitColor(c);
		StelApp::immediateSave("color/cubewano_orbits_color", c.toStr());
		emit cubewanosOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getCubewanosOrbitsColor(void) const
{
	return Planet::getCubewanoOrbitColor();
}

void SolarSystem::setPlutinosOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getPlutinoOrbitColor())
	{
		Planet::setPlutinoOrbitColor(c);
		StelApp::immediateSave("color/plutino_orbits_color", c.toStr());
		emit plutinosOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getPlutinosOrbitsColor(void) const
{
	return Planet::getPlutinoOrbitColor();
}

void SolarSystem::setScatteredDiskObjectsOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getScatteredDiscObjectOrbitColor())
	{
		Planet::setScatteredDiscObjectOrbitColor(c);
		StelApp::immediateSave("color/sdo_orbits_color", c.toStr());
		emit scatteredDiskObjectsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getScatteredDiskObjectsOrbitsColor(void) const
{
	return Planet::getScatteredDiscObjectOrbitColor();
}

void SolarSystem::setOortCloudObjectsOrbitsColor(const Vec3f &c)
{
	if (c!=Planet::getOortCloudObjectOrbitColor())
	{
		Planet::setOortCloudObjectOrbitColor(c);
		StelApp::immediateSave("color/oco_orbits_color", c.toStr());
		emit oortCloudObjectsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getOortCloudObjectsOrbitsColor(void) const
{
	return Planet::getOortCloudObjectOrbitColor();
}

void SolarSystem::setCometsOrbitsColor(const Vec3f& c)
{
	if (c!=Planet::getCometOrbitColor())
	{
		Planet::setCometOrbitColor(c);
		StelApp::immediateSave("color/comet_orbits_color", c.toStr());
		emit cometsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getCometsOrbitsColor(void) const
{
	return Planet::getCometOrbitColor();
}

void SolarSystem::setSednoidsOrbitsColor(const Vec3f& c)
{
	if (c!=Planet::getSednoidOrbitColor())
	{
		Planet::setSednoidOrbitColor(c);
		StelApp::immediateSave("color/sednoid_orbits_color", c.toStr());
		emit sednoidsOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getSednoidsOrbitsColor(void) const
{
	return Planet::getSednoidOrbitColor();
}

void SolarSystem::setInterstellarOrbitsColor(const Vec3f& c)
{
	if (c!=Planet::getInterstellarOrbitColor())
	{
		Planet::setInterstellarOrbitColor(c);
		StelApp::immediateSave("color/interstellar_orbits_color", c.toStr());
		emit interstellarOrbitsColorChanged(c);
	}
}

Vec3f SolarSystem::getInterstellarOrbitsColor(void) const
{
	return Planet::getInterstellarOrbitColor();
}

void SolarSystem::setMercuryOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getMercuryOrbitColor())
	{
		Planet::setMercuryOrbitColor(c);
		StelApp::immediateSave("color/mercury_orbit_color", c.toStr());
		emit mercuryOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getMercuryOrbitColor(void) const
{
	return Planet::getMercuryOrbitColor();
}

void SolarSystem::setVenusOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getVenusOrbitColor())
	{
		Planet::setVenusOrbitColor(c);
		StelApp::immediateSave("color/venus_orbit_color", c.toStr());
		emit venusOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getVenusOrbitColor(void) const
{
	return Planet::getVenusOrbitColor();
}

void SolarSystem::setEarthOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getEarthOrbitColor())
	{
		Planet::setEarthOrbitColor(c);
		StelApp::immediateSave("color/earth_orbit_color", c.toStr());
		emit earthOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getEarthOrbitColor(void) const
{
	return Planet::getEarthOrbitColor();
}

void SolarSystem::setMarsOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getMarsOrbitColor())
	{
		Planet::setMarsOrbitColor(c);
		StelApp::immediateSave("color/mars_orbit_color", c.toStr());
		emit marsOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getMarsOrbitColor(void) const
{
	return Planet::getMarsOrbitColor();
}

void SolarSystem::setJupiterOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getJupiterOrbitColor())
	{
		Planet::setJupiterOrbitColor(c);
		StelApp::immediateSave("color/jupiter_orbit_color", c.toStr());
		emit jupiterOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getJupiterOrbitColor(void) const
{
	return Planet::getJupiterOrbitColor();
}

void SolarSystem::setSaturnOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getSaturnOrbitColor())
	{
		Planet::setSaturnOrbitColor(c);
		StelApp::immediateSave("color/saturn_orbit_color", c.toStr());
		emit saturnOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getSaturnOrbitColor(void) const
{
	return Planet::getSaturnOrbitColor();
}

void SolarSystem::setUranusOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getUranusOrbitColor())
	{
		Planet::setUranusOrbitColor(c);
		StelApp::immediateSave("color/uranus_orbit_color", c.toStr());
		emit uranusOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getUranusOrbitColor(void) const
{
	return Planet::getUranusOrbitColor();
}

void SolarSystem::setNeptuneOrbitColor(const Vec3f &c)
{
	if (c!=Planet::getNeptuneOrbitColor())
	{
		Planet::setNeptuneOrbitColor(c);
		StelApp::immediateSave("color/neptune_orbit_color", c.toStr());
		emit neptuneOrbitColorChanged(c);
	}
}

Vec3f SolarSystem::getNeptuneOrbitColor(void) const
{
	return Planet::getNeptuneOrbitColor();
}

// Set/Get if Moon display is scaled
void SolarSystem::setFlagMoonScale(bool b)
{
	if(b!=flagMoonScale)
	{
		if (b) getMoon()->setSphereScale(moonScale);
		else getMoon()->setSphereScale(1);
		flagMoonScale = b;
		StelApp::immediateSave("viewing/flag_moon_scaled", b);
		emit flagMoonScaleChanged(b);
	}
}

// Set/Get Moon display scaling factor. This goes directly to the Moon object.
void SolarSystem::setMoonScale(double f)
{
	if(!fuzzyEquals(moonScale, f))
	{
		moonScale = f;
		if (flagMoonScale)
			getMoon()->setSphereScale(moonScale);
		StelApp::immediateSave("viewing/moon_scale", f);
		emit moonScaleChanged(f);
	}
}

// Set if minor body display is scaled. This flag will be queried by all Planet objects except for the Moon.
void SolarSystem::setFlagMinorBodyScale(bool b)
{
	if(b!=flagMinorBodyScale)
	{
		flagMinorBodyScale = b;

		double newScale = b ? minorBodyScale : 1.0;
		//update the bodies with the new scale
		for (const auto& p : std::as_const(systemPlanets))
		{
			if(p == moon) continue;
			if (p->getPlanetType()!=Planet::isPlanet && p->getPlanetType()!=Planet::isStar)
				p->setSphereScale(newScale);
		}
		StelApp::immediateSave("viewing/flag_minorbodies_scaled", b);
		emit flagMinorBodyScaleChanged(b);
	}
}

// Set minor body display scaling factor. This will be queried by all Planet objects except for the Moon.
void SolarSystem::setMinorBodyScale(double f)
{
	if(!fuzzyEquals(minorBodyScale, f))
	{
		minorBodyScale = f;
		if(flagMinorBodyScale) //update the bodies with the new scale
		{
			for (const auto& p : std::as_const(systemPlanets))
			{
				if(p == moon) continue;
				if (p->getPlanetType()!=Planet::isPlanet && p->getPlanetType()!=Planet::isStar)
					p->setSphereScale(minorBodyScale);
			}
		}
		StelApp::immediateSave("viewing/minorbodies_scale", f);
		emit minorBodyScaleChanged(f);
	}
}

// Set if Planet display is scaled
void SolarSystem::setFlagPlanetScale(bool b)
{
	if(b!=flagPlanetScale)
	{
		double scale=(b ? planetScale : 1.);
		for (auto& p : systemPlanets)
		{
			if (p->pType==Planet::isPlanet)
				p->setSphereScale(scale);
		}
		flagPlanetScale = b;
		StelApp::immediateSave("viewing/flag_planets_scaled", b);
		emit flagPlanetScaleChanged(b);
	}
}

// Set Moon display scaling factor.
void SolarSystem::setPlanetScale(double f)
{
	if(!fuzzyEquals(planetScale, f))
	{
		planetScale = f;
		if (flagPlanetScale)
			for (auto& p : systemPlanets)
			{
				if (p->pType==Planet::isPlanet)
					p->setSphereScale(planetScale);
			}
		StelApp::immediateSave("viewing/planets_scale", f);
		emit planetScaleChanged(f);
	}
}

// Set if Sun display is scaled
void SolarSystem::setFlagSunScale(bool b)
{
	if(b!=flagSunScale)
	{
		if (b) getSun()->setSphereScale(sunScale);
		else getSun()->setSphereScale(1);
		flagSunScale = b;
		StelApp::immediateSave("viewing/flag_sun_scaled", b);
		emit flagSunScaleChanged(b);
	}
}

// Set Sun display scaling factor. This goes directly to the Sun object.
void SolarSystem::setSunScale(double f)
{
	if(!fuzzyEquals(sunScale, f))
	{
		sunScale = f;
		if (flagSunScale)
			getSun()->setSphereScale(sunScale);
		StelApp::immediateSave("viewing/sun_scale", f);
		emit sunScaleChanged(f);
	}
}

// Set selected planets by englishName
void SolarSystem::setSelected(const QString& englishName)
{
	setSelected(searchByEnglishName(englishName));
}

// Get the list of all the planet english names
QStringList SolarSystem::getAllPlanetEnglishNames() const
{
	QStringList res;
	for (const auto& p : systemPlanets)
		res.append(p->getEnglishName());
	return res;
}

QStringList SolarSystem::getAllPlanetLocalizedNames() const
{
	QStringList res;
	for (const auto& p : systemPlanets)
		res.append(p->getNameI18n());
	return res;
}

QStringList SolarSystem::getAllMinorPlanetEnglishNames() const
{
	QStringList res;
	for (const auto& p : systemMinorBodies)
		res.append(p->getEnglishName());
	return res;
}

// GZ TODO: This could be modified to only delete&reload the minor objects. For now, we really load both parts again like in the 0.10?-0.15 series.
void SolarSystem::reloadPlanets()
{
	// Save flag states
	const bool flagScaleMoon = getFlagMoonScale();
	const double moonScale = getMoonScale();
	const bool flagScaleMinorBodies=getFlagMinorBodyScale();
	const double minorScale= getMinorBodyScale();
	const bool flagPlanets = getFlagPlanets();
	const bool flagHints = getFlagHints();
	const bool flagLabels = getFlagLabels();
	const bool flagOrbits = getFlagOrbits();
	bool hasSelection = false;

	// Save observer location (fix for LP bug # 969211)
	// TODO: This can probably be done better with a better understanding of StelObserver --BM
	StelCore* core = StelApp::getInstance().getCore();
	const StelLocation loc = core->getCurrentLocation();
	StelObjectMgr* objMgr = GETSTELMODULE(StelObjectMgr);

	// Whether any planet are selected? Save the current selection...
	const QList<StelObjectP> selectedObject = objMgr->getSelectedObject("Planet");
	if (!selectedObject.isEmpty())
	{
		// ... unselect current planet.
		hasSelection = true;
		objMgr->unSelect();
	}
	// Unload all Solar System objects
	selected.clear();//Release the selected one

	// GZ TODO in case this methods gets converted to only reload minor bodies: Only delete Orbits which are not referenced by some Planet.
	for (auto* orb : std::as_const(orbits))
	{
		delete orb;
	}
	orbits.clear();

	sun.clear();
	moon.clear();
	earth.clear();
	Planet::texEarthShadow.clear(); //Loaded in loadPlanets()

	delete allTrails;
	allTrails = Q_NULLPTR;

	for (const auto& p : std::as_const(systemPlanets))
	{
		p->satellites.clear();
	}
	systemPlanets.clear();
	systemMinorBodies.clear();
	// Memory leak? What's the proper way of cleaning shared pointers?

	// Also delete Comet textures (loaded in loadPlanets()
	Comet::tailTexture.clear();
	Comet::comaTexture.clear();

	// Re-load the ssystem_major.ini and ssystem_minor.ini file
	loadPlanets();	
	computePositions(core, core->getJDE(), getSun());
	setSelected("");
	recreateTrails();
	
	// Restore observer location
	core->moveObserverTo(loc, 0., 0.);

	// Restore flag states
	setFlagMoonScale(flagScaleMoon);
	setMoonScale(moonScale);
	setFlagMinorBodyScale(flagScaleMinorBodies);
	setMinorBodyScale(1.0); // force-reset first to really reach the objects in the next call.
	setMinorBodyScale(minorScale);
	setFlagPlanets(flagPlanets);
	setFlagHints(flagHints);
	setFlagLabels(flagLabels);
	setFlagOrbits(flagOrbits);

	// Restore translations
	updateI18n();

	if (hasSelection)
	{
		// Restore selection...
		StelObjectP obj = selectedObject[0];
		objMgr->findAndSelect(obj->getEnglishName(), obj->getType());
	}

	emit solarSystemDataReloaded();
}

// Set the algorithm for computation of apparent magnitudes for planets in case  observer on the Earth
void SolarSystem::setApparentMagnitudeAlgorithmOnEarth(const QString &algorithm)
{
	Planet::ApparentMagnitudeAlgorithm id=vMagAlgorithmMap.key(algorithm);
	Planet::setApparentMagnitudeAlgorithm(id);
	StelApp::immediateSave("astro/apparent_magnitude_algorithm", algorithm);
	emit apparentMagnitudeAlgorithmOnEarthChanged(algorithm);
}
// overloaded for GUI efficiency
void SolarSystem::setApparentMagnitudeAlgorithmOnEarth(const Planet::ApparentMagnitudeAlgorithm id)
{
	QString name =vMagAlgorithmMap.value(id);
	Planet::setApparentMagnitudeAlgorithm(id);
	StelApp::immediateSave("astro/apparent_magnitude_algorithm", name);
	emit apparentMagnitudeAlgorithmOnEarthChanged(name);
}

// Get the algorithm used for computation of apparent magnitudes for planets in case  observer on the Earth
QString SolarSystem::getApparentMagnitudeAlgorithmOnEarth() const
{
	return vMagAlgorithmMap.value(Planet::getApparentMagnitudeAlgorithm());
}

void SolarSystem::setFlagDrawMoonHalo(bool b)
{
	Planet::drawMoonHalo=b;
	StelApp::immediateSave("viewing/flag_draw_moon_halo", b);
	emit flagDrawMoonHaloChanged(b);
}

bool SolarSystem::getFlagDrawMoonHalo() const
{
	return Planet::drawMoonHalo;
}

void SolarSystem::setFlagDrawSunHalo(bool b)
{
	Planet::drawSunHalo=b;
	StelApp::immediateSave("viewing/flag_draw_sun_halo", b);
	emit flagDrawSunHaloChanged(b);
}

bool SolarSystem::getFlagDrawSunHalo() const
{
	return Planet::drawSunHalo;
}

void SolarSystem::setFlagPermanentOrbits(bool b)
{
	if (Planet::permanentDrawingOrbits!=b)
	{
		Planet::permanentDrawingOrbits=b;
		StelApp::immediateSave("astro/flag_permanent_orbits", b);
		emit flagPermanentOrbitsChanged(b);
	}
}

bool SolarSystem::getFlagPermanentOrbits() const
{
	return Planet::permanentDrawingOrbits;
}

void SolarSystem::setOrbitsThickness(int v)
{
	if (v!=Planet::orbitsThickness)
	{
		Planet::orbitsThickness=v;
		StelApp::immediateSave("astro/object_orbits_thickness", v);
		emit orbitsThicknessChanged(v);
	}
}

int SolarSystem::getOrbitsThickness() const
{
	return Planet::orbitsThickness;
}

void SolarSystem::setGrsLongitude(int longitude)
{
	RotationElements::grsLongitude = longitude;
	// automatic saving of the setting
	conf->setValue("astro/grs_longitude", longitude);
	emit grsLongitudeChanged(longitude);
}

int SolarSystem::getGrsLongitude() const
{
	return static_cast<int>(RotationElements::grsLongitude);
}

void SolarSystem::setGrsDrift(double drift)
{
	RotationElements::grsDrift = drift;
	// automatic saving of the setting
	conf->setValue("astro/grs_drift", drift);
	emit grsDriftChanged(drift);
}

double SolarSystem::getGrsDrift() const
{
	return RotationElements::grsDrift;
}

void SolarSystem::setGrsJD(double JD)
{
	RotationElements::grsJD = JD;
	// automatic saving of the setting
	conf->setValue("astro/grs_jd", JD);
	emit grsJDChanged(JD);
}

double SolarSystem::getGrsJD()
{
	return RotationElements::grsJD;
}

void SolarSystem::setFlagEarthShadowEnlargementDanjon(bool b)
{
	earthShadowEnlargementDanjon=b;
	StelApp::immediateSave("astro/shadow_enlargement_danjon", b);
	emit earthShadowEnlargementDanjonChanged(b);
}

bool SolarSystem::getFlagEarthShadowEnlargementDanjon() const
{
	return earthShadowEnlargementDanjon;
}

void SolarSystem::setOrbitColorStyle(const QString &style)
{
	static const QMap<QString, Planet::PlanetOrbitColorStyle>map={
		{ QString("groups"),                    Planet::ocsGroups},
		{ QString("major_planets"),             Planet::ocsMajorPlanets},
		{ QString("major_planets_minor_types"), Planet::ocsMajorPlanetsMinorTypes}
	};
	Planet::PlanetOrbitColorStyle st=map.value(style.toLower(), Planet::ocsOneColor);
	Planet::orbitColorStyle = st;
	StelApp::immediateSave("astro/planets_orbits_color_style", style);
	emit orbitColorStyleChanged(style);
}

QString SolarSystem::getOrbitColorStyle() const
{
	static const QMap<Planet::PlanetOrbitColorStyle, QString>map={
		{ Planet::ocsOneColor,               "one_color"},
		{ Planet::ocsGroups,                 "groups"},
		{ Planet::ocsMajorPlanets,           "major_planets"},
		{ Planet::ocsMajorPlanetsMinorTypes, "major_planets_minor_types"},
	};
	return map.value(Planet::orbitColorStyle, "one_color");
}

// TODO: To make the code better understandable, get rid of planet->computeModelMatrix(trans, true) here.
QPair<double, PlanetP> SolarSystem::getSolarEclipseFactor(const StelCore* core) const
{
	PlanetP p;
	const Vec3d Lp = sun->getEclipticPos() + sun->getAberrationPush();
	const Vec3d P3 = core->getObserverHeliocentricEclipticPos();
	const double RS = sun->getEquatorialRadius();

	double final_illumination = 1.0;

	for (const auto& planet : systemPlanets)
	{
		if(planet == sun || planet == core->getCurrentPlanet())
			continue;

		// Seen from Earth, only Moon, Venus or Mercury are relevant. The rest can be thrown away. There is no asteroid to go in front of the sun...
		static const QStringList fromEarth({"Moon", "Mercury", "Venus"});
		if ((core->getCurrentPlanet() == earth) && !fromEarth.contains(planet->englishName))
			continue;

		Mat4d trans;
		planet->computeModelMatrix(trans, true);

		const Vec3d C = trans * Vec3d(0., 0., 0.);
		const double radius = planet->getEquatorialRadius();

		Vec3d v1 = Lp - P3;
		Vec3d v2 = C - P3;
		const double L = v1.norm();
		const double l = v2.norm();
		v1 /= L;
		v2 /= l;

		const double R = RS / L;
		const double r = radius / l;
		const double d = ( v1 - v2 ).norm();
		double illumination;

		if(d >= R + r) // distance too far
		{
			illumination = 1.0;
		}
		else if(d <= r - R) // umbra
		{
			illumination = 0.0;
		}
		else if(d <= R - r) // penumbra completely inside
		{
			illumination = 1.0 - r * r / (R * R);
		}
		else // penumbra partially inside
		{
			const double x = (R * R + d * d - r * r) / (2.0 * d);

			const double alpha = std::acos(x / R);
			const double beta = std::acos((d - x) / r);

			const double AR = R * R * (alpha - 0.5 * std::sin(2.0 * alpha));
			const double Ar = r * r * (beta - 0.5 * std::sin(2.0 * beta));
			const double AS = R * R * 2.0 * std::asin(1.0);

			illumination = 1.0 - (AR + Ar) / AS;
		}

		if(illumination < final_illumination)
		{
			final_illumination = illumination;
			p = planet;
		}
	}

	return QPair<double, PlanetP>(final_illumination, p);
}

// Opening angle of the bright Solar crescent, radians
// From: J. Meeus, Morsels IV, ch.15
// lunarSize: apparent Lunar radius or diameter, angular units of your preference
// solarSize: apparent Solar radius or diameter, resp., same angular units
// eclipseMagnitude: covered fraction of the Solar diameter.
double SolarSystem::getEclipseCrescentAngle(const double lunarSize, const double solarSize, const double eclipseMagnitude)
{
	const double R = lunarSize/solarSize;
	const double cosAhalf = 2.*eclipseMagnitude * (R-eclipseMagnitude)/(1.+R-2.*eclipseMagnitude) - 1.;
	return (std::fabs(cosAhalf) <= 1. ? 2.*acos(cosAhalf) : 0.);
}

// Retrieve Radius of Umbra and Penumbra at the distance of the Moon.
// Returns a pair (umbra, penumbra) in (geocentric_arcseconds, AU, geometric_AU).
// * sizes in arcseconds are the usual result found as Bessel element in eclipse literature.
//   It includes scaling for effects of atmosphere either after Chauvenet (2%) or after Danjon. (see Espenak: 5000 Years Canon of Lunar Eclipses.)
// * sizes in AU are the same, converted back to AU in Lunar distance.
// * sizes in geometric_AU derived from pure geometrical evaluations without scalings applied.
QPair<Vec3d,Vec3d> SolarSystem::getEarthShadowRadiiAtLunarDistance() const
{
	// Note: The application of this shadow enlargement is not according to the books, but looks close enough for now.
	static const double sun2earth=sun->getEquatorialRadius() / earth->getEquatorialRadius();
	PlanetP sun=getSun();
	PlanetP moon=getMoon();
	PlanetP earth=getEarth();
	const double lunarDistance=moon->getEclipticPos().norm(); // Lunar distance [AU]
	const double earthDistance=earth->getHeliocentricEclipticPos().norm(); // Earth distance [AU]
	const double sunHP =asin(earth->getEquatorialRadius()/earthDistance) * M_180_PI*3600.; // arcsec.
	const double moonHP=asin(earth->getEquatorialRadius()/lunarDistance) * M_180_PI*3600.; // arcsec.
	const double sunSD  =atan(sun->getEquatorialRadius()/earthDistance)  * M_180_PI*3600.; // arcsec.

	// Compute umbra radius at lunar distance.
	const double lUmbra=earthDistance/(sun2earth-1.); // length of earth umbra [AU]
	const double rUmbraAU=earth->getEquatorialRadius()*(lUmbra-lunarDistance)/lUmbra; // radius of earth shadow at lunar distance [AU]
	// Penumbra:
	const double lPenumbra=earthDistance/(sun2earth + 1.); // distance between earth and point between sun and earth where penumbral border rays intersect
	const double rPenumbraAU=earth->getEquatorialRadius()*(lPenumbra+lunarDistance)/lPenumbra; // radius of penumbra at Lunar distance [AU]

	//Classical Bessel elements instead
	double f1, f2;
	if (earthShadowEnlargementDanjon)
	{
		static const double danjonScale=1+1./85.-1./594.; // ~1.01, shadow magnification factor (see Espenak 5000 years Canon)
		f1=danjonScale*moonHP + sunHP + sunSD; // penumbra radius, arcsec
		f2=danjonScale*moonHP + sunHP - sunSD; // umbra radius, arcsec
	}
	else
	{
		const double mHP1=0.998340*moonHP;
		f1=1.02*(mHP1 + sunHP + sunSD); // penumbra radius, arcsec
		f2=1.02*(mHP1 + sunHP - sunSD); // umbra radius, arcsec
	}
	const double f1_AU=tan(f1/3600.*M_PI_180)*lunarDistance;
	const double f2_AU=tan(f2/3600.*M_PI_180)*lunarDistance;
	return QPair<Vec3d,Vec3d>(Vec3d(f2, f2_AU, rUmbraAU), Vec3d(f1, f1_AU, rPenumbraAU));
}

bool SolarSystem::removeMinorPlanet(const QString &name)
{
	PlanetP candidate = searchMinorPlanetByEnglishName(name);
	if (!candidate)
	{
		qWarning() << "Cannot remove planet " << name << ": Not found.";
		return false;
	}

	Orbit* orbPtr=static_cast<Orbit*>(candidate->orbitPtr);
	if (orbPtr)
		orbits.removeOne(orbPtr);
	systemPlanets.removeOne(candidate);
	systemMinorBodies.removeOne(candidate);
	candidate.clear();
	return true;
}

void SolarSystem::onNewSurvey(HipsSurveyP survey)
{
	if (!survey->isPlanetarySurvey()) return;

	const auto type = survey->getType();
	const bool isPlanetColor = type == "planet";
	const bool isPlanetNormal = type == "planet-normal";
	const bool isPlanetHorizon = type == "planet-horizon";
	if (!isPlanetColor && !isPlanetNormal && !isPlanetHorizon)
		return;

	QString planetName = survey->getFrame();
	PlanetP pl = searchByEnglishName(planetName);
	if (!pl) return;
	if (isPlanetColor)
	{
		if (pl->survey) return;
		pl->survey = survey;
	}
	else if (isPlanetNormal)
	{
		if (pl->surveyForNormals) return;
		pl->surveyForNormals = survey;
	}
	else if (isPlanetHorizon)
	{
		if (pl->surveyForHorizons) return;
		pl->surveyForHorizons = survey;
	}
	survey->setProperty("planet", pl->getEnglishName());
	// Not visible by default for the moment.
	survey->setProperty("visible", false);
}

void SolarSystem::setExtraThreads(int n)
{
	extraThreads=qBound(0,n,QThreadPool::globalInstance()->maxThreadCount()-1);
	StelApp::immediateSave("astro/solar_system_threads", extraThreads);
	emit extraThreadsChanged(extraThreads);
}

void SolarSystem::setMarkerMagThreshold(double m)
{
	markerMagThreshold=qBound(-5.,m,37.); // sync with GUI & WUI!
	StelApp::immediateSave("astro/planet_markers_mag_threshold", markerMagThreshold);
	emit markerMagThresholdChanged(markerMagThreshold);
}

const QMap<Planet::ApparentMagnitudeAlgorithm, QString> SolarSystem::vMagAlgorithmMap =
{
	{Planet::MallamaHilton_2018,	        "Mallama2018"},
	{Planet::ExplanatorySupplement_2013,	"ExpSup2013"},
	{Planet::ExplanatorySupplement_1992,	"ExpSup1992"},
	{Planet::Mueller_1893,			"Mueller1893"},
	{Planet::AstronomicalAlmanac_1984,	"AstrAlm1984"},
	{Planet::Generic,			"Generic"},
	{Planet::UndefinedAlgorithm,		""}
};
