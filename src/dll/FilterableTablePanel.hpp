#pragma once

/**
 * Abstract base class for tab panels with filterable tables.
 * Provides a template method pattern for panels that need filtering UI and tables.
 * Future panels (PropPlopPanel, FloraPlopPanel, etc.) can inherit from this.
 */
class FilterableTablePanel {
public:
    virtual ~FilterableTablePanel() = default;

    /**
     * Lifecycle methods - to be implemented by derived classes
     */
    virtual void OnInit() = 0;
    virtual void OnRender() = 0;
    virtual void OnShutdown() = 0;

protected:
    /**
     * Template methods for derived classes to implement.
     * These define the structure of the panel rendering.
     */

    /**
     * Render the filter UI controls (search, sliders, dropdowns, etc.)
     */
    virtual void RenderFilterUI_() = 0;

    /**
     * Render the main table with filtered items
     */
    virtual void RenderTable_() = 0;
};