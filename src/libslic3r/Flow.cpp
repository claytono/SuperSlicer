#include "Flow.hpp"
#include "I18N.hpp"
#include "Print.hpp"
#include "Layer.hpp"
#include <cmath>
#include <assert.h>

#include <boost/algorithm/string/predicate.hpp>

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

FlowErrorNegativeSpacing::FlowErrorNegativeSpacing() : 
	FlowError("Flow::spacing() produced negative spacing. Did you set some extrusion width too small?") {}

FlowErrorNegativeFlow::FlowErrorNegativeFlow() :
    FlowError("Flow::mm3_per_mm() produced negative flow. Did you set some extrusion width too small?") {}

// This static method returns a sane extrusion width default.
float Flow::auto_extrusion_width(FlowRole role, float nozzle_diameter)
{
    switch (role) {
    case frSupportMaterial:
    case frSupportMaterialInterface:
    case frTopSolidInfill:
    case frExternalPerimeter:
        return 1.05f * nozzle_diameter;
    default:
    case frPerimeter:
    case frSolidInfill:
    case frInfill:
        return 1.125f * nozzle_diameter;
    }
}

// Used by the Flow::extrusion_width() funtion to provide hints to the user on default extrusion width values,
// and to provide reasonable values to the PlaceholderParser.
static inline FlowRole opt_key_to_flow_role(const std::string &opt_key)
{
 	if (opt_key == "perimeter_extrusion_width" || 
 		// or all the defaults:
 		opt_key == "extrusion_width" || opt_key == "first_layer_extrusion_width")
        return frPerimeter;
    else if (opt_key == "external_perimeter_extrusion_width")
        return frExternalPerimeter;
    else if (opt_key == "infill_extrusion_width")
        return frInfill;
    else if (opt_key == "solid_infill_extrusion_width")
        return frSolidInfill;
	else if (opt_key == "top_infill_extrusion_width")
		return frTopSolidInfill;
	else if (opt_key == "support_material_extrusion_width")
    	return frSupportMaterial;
    else 
    	throw Slic3r::RuntimeError("opt_key_to_flow_role: invalid argument");
};

static inline void throw_on_missing_variable(const std::string &opt_key, const char *dependent_opt_key) 
{
	throw FlowErrorMissingVariable((boost::format(L("Cannot calculate extrusion width for %1%: Variable \"%2%\" not accessible.")) % opt_key % dependent_opt_key).str());
}

// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionFloatOrPercent* opt, const ConfigOptionResolver& config, const unsigned int first_printing_extruder)
{
    assert(opt != nullptr);

    bool first_layer = boost::starts_with(opt_key, "first_layer_") || boost::starts_with(opt_key, "brim_");
    if (!first_layer && boost::starts_with(opt_key, "skirt_")) {
        const ConfigOptionInt* optInt = config.option<ConfigOptionInt>("skirt_height");
        const ConfigOptionBool* optBool = config.option<ConfigOptionBool>("draft_shield");
        first_layer = (optBool && optInt && optInt->value == 1 && !optBool->value);
    }

    if (opt->value == 0.) {
        // The role specific extrusion width value was set to zero, get a not-0 one (if possible)
        opt = extrusion_option(opt_key, config);
    }

    if (opt->percent) {
        auto opt_key_layer_height = first_layer ? "first_layer_height" : "layer_height";
        auto opt_layer_height = config.option(opt_key_layer_height);
        if (opt_layer_height == nullptr)
            throw_on_missing_variable(opt_key, opt_key_layer_height);
        // first_layer_height depends on first_printing_extruder
        auto opt_nozzle_diameters = config.option<ConfigOptionFloats>("nozzle_diameter");
        if (opt_nozzle_diameters == nullptr)
            throw_on_missing_variable(opt_key, "nozzle_diameter");
        return opt->get_abs_value(float(opt_nozzle_diameters->get_at(first_printing_extruder)));
    }

    if (opt->value == 0.) {
        // If user left option to 0, calculate a sane default width.
        auto opt_nozzle_diameters = config.option<ConfigOptionFloats>("nozzle_diameter");
        if (opt_nozzle_diameters == nullptr)
            throw_on_missing_variable(opt_key, "nozzle_diameter");
        return auto_extrusion_width(opt_key_to_flow_role(opt_key), float(opt_nozzle_diameters->get_at(first_printing_extruder)));
    }

    return opt->value;
}

//used to get brim & skirt extrusion config
const ConfigOptionFloatOrPercent* Flow::extrusion_option(const std::string& opt_key, const ConfigOptionResolver& config)
{

    const ConfigOptionFloatOrPercent* opt = config.option<ConfigOptionFloatOrPercent>(opt_key);

    //brim is first_layer_extrusion_width then perimeter_extrusion_width
    if (!opt && boost::starts_with(opt_key, "brim_")) {
        const ConfigOptionFloatOrPercent* optTest = config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
        opt = optTest;
        if (opt == nullptr)
            throw_on_missing_variable(opt_key, "first_layer_extrusion_width");
        if (opt->value == 0) {
            opt = config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
            if (opt == nullptr)
                throw_on_missing_variable(opt_key, "perimeter_extrusion_width");
        }
    }

    if (opt == nullptr)
        throw_on_missing_variable(opt_key, opt_key.c_str());

    // This is the logic used for skit / brim, but not for the rest of the 1st layer.
	if (opt->value == 0. && boost::starts_with(opt_key, "skirt")) {
        // The "skirt_extrusion_width" was set to zero, try a substitute.
        const ConfigOptionFloatOrPercent* optTest = config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
        const ConfigOptionInt* optInt = config.option<ConfigOptionInt>("skirt_height");
        const ConfigOptionBool* optBool = config.option<ConfigOptionBool>("draft_shield");
        if (optTest == nullptr)
            throw_on_missing_variable(opt_key, "first_layer_extrusion_width");
        if (optBool == nullptr)
            throw_on_missing_variable(opt_key, "draft_shield");
        if (optInt == nullptr)
            throw_on_missing_variable(opt_key, "skirt_height");
        // The "first_layer_extrusion_width" was set to zero, try a substitute.
        if (optTest && optBool && optInt && optTest->value > 0 && optInt->value == 1 && !optBool->value)
            opt = optTest;

        if (opt->value == 0) {
            opt = config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
            if (opt == nullptr)
                throw_on_missing_variable(opt_key, "perimeter_extrusion_width");
        }
	}

    // external_perimeter_extrusion_width default is perimeter_extrusion_width
    if (opt->value == 0. && boost::starts_with(opt_key, "external_perimeter_extrusion_width")) {
        // The role specific extrusion width value was set to zero, try the role non-specific extrusion width.
        opt = config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
        if (opt == nullptr)
            throw_on_missing_variable(opt_key, "perimeter_extrusion_width");
    }

    // top_infill_extrusion_width default is solid_infill_extrusion_width
    if (opt->value == 0. && boost::starts_with(opt_key, "top_infill_extrusion_width")) {
        // The role specific extrusion width value was set to zero, try the role non-specific extrusion width.
        opt = config.option<ConfigOptionFloatOrPercent>("solid_infill_extrusion_width");
        if (opt == nullptr)
            throw_on_missing_variable(opt_key, "perimeter_extrusion_width");
    }

	if (opt->value == 0.) {
		// The role specific extrusion width value was set to zero, try the role non-specific extrusion width.
		opt = config.option<ConfigOptionFloatOrPercent>("extrusion_width");
		if (opt == nullptr)
    		throw_on_missing_variable(opt_key, "extrusion_width");
	}

	return opt;
}

// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionResolver &config, const unsigned int first_printing_extruder)
{
    return extrusion_width(opt_key, config.option<ConfigOptionFloatOrPercent>(opt_key), config, first_printing_extruder);
}

// This constructor builds a Flow object from an extrusion width config setting
// and other context properties.
Flow Flow::new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height, float spacing_ratio, float bridge_flow_ratio)
{
    // we need layer height unless it's a bridge
    if (height <= 0 && bridge_flow_ratio == 0) 
        throw Slic3r::InvalidArgument("Invalid flow height supplied to new_from_config_width()");

    float w;
    if (bridge_flow_ratio > 0) {
        // If bridge flow was requested, calculate the bridge width.
        height = w = (bridge_flow_ratio == 1.) ?
            // optimization to avoid sqrt()
            nozzle_diameter :
            sqrt(bridge_flow_ratio) * nozzle_diameter;
    } else if (! width.percent && width.value <= 0.) {
        // If user left option to 0, calculate a sane default width.
        w = auto_extrusion_width(role, nozzle_diameter);
    } else {
        // If user set a manual value, use it.
        w = float(width.get_abs_value(nozzle_diameter));
    }
    
    return Flow(w, height, nozzle_diameter, spacing_ratio, bridge_flow_ratio > 0);
}

// This constructor builds a Flow object from a given centerline spacing.
Flow Flow::new_from_spacing(float spacing, float nozzle_diameter, float height, float spacing_ratio, bool bridge)
{
    // we need layer height unless it's a bridge
    if (height <= 0 && !bridge) 
        throw Slic3r::InvalidArgument("Invalid flow height supplied to new_from_spacing()");
    // Calculate width from spacing.
    // For normal extrusons, extrusion width is wider than the spacing due to the rounding and squishing of the extrusions.
    // For bridge extrusions, the extrusions are placed with a tiny BRIDGE_EXTRA_SPACING gaps between the threads.
    float width = float(bridge ?
        (spacing - BRIDGE_EXTRA_SPACING_MULT * nozzle_diameter) :
#ifdef HAS_PERIMETER_LINE_OVERLAP
        (spacing + PERIMETER_LINE_OVERLAP_FACTOR * height * (1. - 0.25 * PI) * spacing_ratio);
#else
        (spacing + height * (1. - 0.25 * PI) * spacing_ratio));
#endif
    return Flow(width, bridge ? width : height, nozzle_diameter, spacing_ratio, bridge);
}

// This method returns the centerline spacing between two adjacent extrusions 
// having the same extrusion width (and other properties).
float Flow::spacing() const 
{
#ifdef HAS_PERIMETER_LINE_OVERLAP
    if (this->bridge)
        return this->width + BRIDGE_EXTRA_SPACING;
    // rectangle with semicircles at the ends
    float min_flow_spacing = this->width - this->height * (1. - 0.25 * PI) * spacing_ratio;
    float res = this->width - PERIMETER_LINE_OVERLAP_FACTOR * (this->width - min_flow_spacing);
#else
    float res = float(this->bridge ? (this->width + BRIDGE_EXTRA_SPACING_MULT * nozzle_diameter) : (this->width - this->height * (1. - 0.25 * PI) * spacing_ratio));
#endif
//    assert(res > 0.f);
	if (res <= 0.f)
		throw FlowErrorNegativeSpacing();
	return res;
}

// This method returns the centerline spacing between an extrusion using this
// flow and another one using another flow.
// this->spacing(other) shall return the same value as other.spacing(*this)
float Flow::spacing(const Flow &other) const
{
    assert(this->height == other.height);
    assert(this->bridge == other.bridge);
    float res = float(this->bridge ? 
        0.5 * this->width + 0.5 * other.width + BRIDGE_EXTRA_SPACING_MULT * nozzle_diameter :
        0.5 * this->spacing() + 0.5 * other.spacing());
//    assert(res > 0.f);
	if (res <= 0.f)
		throw FlowErrorNegativeSpacing();
	return res;
}

// This method returns extrusion volume per head move unit.
double Flow::mm3_per_mm() const 
{
    float res = this->bridge ?
        // Area of a circle with dmr of this->width.
        float((this->width * this->width) * 0.25 * PI) :
        // Rectangle with semicircles at the ends. ~ h (w - 0.215 h)
        float(this->height * (this->width - this->height * (1. - 0.25 * PI)));
    //assert(res > 0.);
	if (res <= 0.)
		throw FlowErrorNegativeFlow();
    return res;
}

Flow support_material_flow(const PrintObject *object, float layer_height)
{
    int extruder_id = object->config().support_material_extruder.value - 1;
    if (extruder_id < 0) {
        extruder_id = object->layers().front()->get_region(0)->region()->config().perimeter_extruder - 1;
    }
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config().support_material_extrusion_width.value > 0) ? object->config().support_material_extrusion_width : object->config().extrusion_width,
        // if object->config().support_material_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(object->print()->config().nozzle_diameter.get_at(extruder_id)),
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value),
        extruder_id < 0 ? 1 : object->config().get_computed_value("filament_max_overlap", extruder_id), //if can get an extruder, then use its param, or use full overlap if we don't know the extruder id.
        // bridge_flow_ratio
        0.f);
}

Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height)
{
    const auto &width = (object->config().first_layer_extrusion_width.value > 0) ? object->config().first_layer_extrusion_width : object->config().support_material_extrusion_width;
    float slice_height = layer_height;
    if (layer_height <= 0.f && !object->print()->config().nozzle_diameter.empty()){
        slice_height = (float)object->get_first_layer_height();
    }
    int extruder_id = object->config().support_material_extruder.value -1;
    if (extruder_id < 0) {
        extruder_id = object->layers().front()->get_region(0)->region()->config().infill_extruder - 1;
    }
    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (width.value > 0) ? width : object->config().extrusion_width,
        float(object->print()->config().nozzle_diameter.get_at(extruder_id)),
        slice_height,
        extruder_id < 0 ? 1 : object->config().get_computed_value("filament_max_overlap", extruder_id), //if can get an extruder, then use its param, or use full overlap if we don't know the extruder id.
        // bridge_flow_ratio
        0.f);
}

Flow support_material_interface_flow(const PrintObject *object, float layer_height)
{
    int extruder_id = object->config().support_material_interface_extruder.value - 1;
    if (extruder_id < 0) {
        extruder_id = object->layers().front()->get_region(0)->region()->config().infill_extruder - 1;
    }
    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config().support_material_extrusion_width > 0) ? object->config().support_material_extrusion_width : object->config().extrusion_width,
        // if object->config().support_material_interface_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), get_at will return the 0th component.
        float(object->print()->config().nozzle_diameter.get_at(extruder_id)),
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value),
        extruder_id < 0 ? 1 : object->config().get_computed_value("filament_max_overlap", extruder_id), //if can get an extruder, then use its param, or use full overlap if we don't know the extruder id.
        // bridge_flow_ratio
        0.f);
}

}
