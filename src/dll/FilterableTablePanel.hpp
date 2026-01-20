#pragma once

/**
 * Abstract base class for tab panels with filterable tables.
 * Provides a template method pattern for panels that need filtering UI and tables.
 * Future panels (PropPlopPanel, FloraPlopPanel, etc.) ideally inherit from this.
 */
class FilterableTablePanel {
public:
    virtual ~FilterableTablePanel() = default;

    virtual void OnInit() = 0;
    virtual void OnRender() = 0;
    virtual void OnShutdown() = 0;

protected:
    virtual void RenderFilterUI_() = 0;
    virtual void RenderTable_() = 0;
};