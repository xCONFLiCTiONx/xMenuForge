using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;
using xMenuForge.Settings.Controls;
using xMenuForge.Settings.Models;
using xMenuForge.Settings.Services;

namespace xMenuForge.Settings;

public partial class MainWindow : Window
{
    private AppConfig _config = new();
    private MenuItemConfig? _selectedCustom;
    private MenuItemConfig? _selectedRuleCmd;
    private Point _dragStartPoint;

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

    public MainWindow()
    {
        InitializeComponent();
        SourceInitialized += (_, _) => ApplyDwmDarkFrame();
        Loaded += async (_, _) =>
        {
            _config = ConfigService.Load();
            FitToWorkArea();
            if (_config.Appearance.WindowMaximized)
            {
                WindowState = WindowState.Maximized;
            }
            BuildCategoryRuleCheckboxes();
            RefreshAllViews();
            await RefreshHealthAsync();
        };
    }

    private void FitToWorkArea()
    {
        var wa = SystemParameters.WorkArea;
        MaxHeight = wa.Height + 14;
        MaxWidth = wa.Width + 14;
        double targetW = Math.Min(Width, wa.Width - 36);
        double targetH = Math.Min(Height, wa.Height - 36);
        if (targetW >= MinWidth) Width = targetW;
        if (targetH >= MinHeight) Height = targetH;
        Left = wa.Left + Math.Max(0, (wa.Width - Width) / 2);
        Top = wa.Top + Math.Max(0, (wa.Height - Height) / 2);
    }

    private void ApplyDwmDarkFrame()
    {
        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd == IntPtr.Zero) return;
        int useDark = 1;
        DwmSetWindowAttribute(hwnd, 20, ref useDark, sizeof(int)); // DWMWA_USE_IMMERSIVE_DARK_MODE
        int cornerPref = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33, ref cornerPref, sizeof(int));
    }

    private void SaveConfigWithFeedback(string status = "All changes saved")
    {
        NormalizeOrder();
        ConfigService.Save(_config);
        SaveStatusText.Text = $"{status} ({DateTime.Now:HH:mm:ss})";
        RefreshOverviewCounts();
        RefreshLivePreview();
    }

    private void NormalizeOrder()
    {
        int i = 0;
        foreach (var item in _config.Items)
        {
            item.Order = i++;
        }
    }

    private void RefreshAllViews()
    {
        RefreshOverviewCounts();
        RefreshLogs();
        RefreshDesignerList();
        RefreshBuiltInCommands();
        RefreshCustomCommandsList();
        RefreshRulesCommandList();
        LoadExecutionFields();
        LoadAppearanceFields();
        LoadIntegrationsFields();
        RefreshLivePreview();
    }

    // =========================================================================
    // Window Chrome & Navigation
    // =========================================================================

    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void Maximize_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    }

    private void Window_StateChanged(object sender, EventArgs e)
    {
        bool isMax = WindowState == WindowState.Maximized;
        if (RootFrameBorder != null)
        {
            RootFrameBorder.Padding = isMax ? new Thickness(7) : new Thickness(0);
        }
        if (MaxGlyph != null)
        {
            MaxGlyph.Data = Geometry.Parse(isMax
                ? "M1,3 L7,3 L7,9 L1,9 Z M3,1 L9,1 L9,7"
                : "M1,1 L9,1 L9,9 L1,9 Z");
        }
        if (_config.Items.Count > 0 && _config.Appearance.WindowMaximized != isMax)
        {
            _config.Appearance.WindowMaximized = isMax;
            ConfigService.Save(_config);
        }
    }

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
    {
        if (_config.Items.Count > 0)
        {
            _config.Appearance.WindowMaximized = WindowState == WindowState.Maximized;
            ConfigService.Save(_config);
        }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    private void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (PageOverview == null || sender is not RadioButton rb || rb.Tag is not string tag) return;

        PageOverview.Visibility = tag == "Overview" ? Visibility.Visible : Visibility.Collapsed;
        PageMenuDesigner.Visibility = tag == "MenuDesigner" ? Visibility.Visible : Visibility.Collapsed;
        PageCommands.Visibility = tag == "Commands" ? Visibility.Visible : Visibility.Collapsed;
        PageCustomCommands.Visibility = tag == "CustomCommands" ? Visibility.Visible : Visibility.Collapsed;
        PageExecution.Visibility = tag == "Execution" ? Visibility.Visible : Visibility.Collapsed;
        PageAppearance.Visibility = tag == "Appearance" ? Visibility.Visible : Visibility.Collapsed;
        PageContextRules.Visibility = tag == "ContextRules" ? Visibility.Visible : Visibility.Collapsed;
        PageIntegrations.Visibility = tag == "Integrations" ? Visibility.Visible : Visibility.Collapsed;
        PageDiagnostics.Visibility = tag == "Diagnostics" ? Visibility.Visible : Visibility.Collapsed;

        if (tag == "MenuDesigner")
        {
            RefreshDesignerList();
            RefreshLivePreview();
        }
        else if (tag == "Diagnostics" || tag == "Overview")
        {
            RefreshLogs();
        }
    }

    private void QuickNewCustom_Click(object sender, RoutedEventArgs e)
    {
        NavCustom.IsChecked = true;
        AddCustomCommand_Click(sender, e);
    }

    private void QuickOpenDesigner_Click(object sender, RoutedEventArgs e) => NavDesigner.IsChecked = true;

    // =========================================================================
    // 1. Overview & Logs
    // =========================================================================

    private void RefreshOverviewCounts()
    {
        int totalCmds = _config.Items.Count(i => i.Kind == "command" && i.Enabled);
        int customCmds = _config.Items.Count(i => i.Kind == "command" && !i.BuiltIn);
        OverviewCmdCount.Text = $"{totalCmds} enabled";
        OverviewCustomCount.Text = $"{customCmds} custom command(s) configured";
        OverviewSubmenuMode.Text = _config.Appearance.ModernSubmenuMode == "flatten"
            ? "Flatten + Separators"
            : "Category Flyout";
    }

    private void RefreshLogs()
    {
        var logs = ConfigService.LoadExecutionLogs(80);
        OverviewActivityList.Items.Clear();
        DiagnosticsLogList.Items.Clear();

        if (logs.Count == 0)
        {
            var empty = new TextBlock { Text = "No command executions recorded yet.", Foreground = (Brush)FindResource("TextMutedBrush"), Margin = new Thickness(6) };
            OverviewActivityList.Items.Add(empty);
            DiagnosticsLogList.Items.Add(new TextBlock { Text = "No execution logs yet. Run a command from Explorer or Test Run a custom command.", Foreground = (Brush)FindResource("TextMutedBrush"), Margin = new Thickness(6) });
            return;
        }

        foreach (var log in logs.Take(12))
        {
            OverviewActivityList.Items.Add(BuildLogRow(log, compact: true));
        }
        foreach (var log in logs)
        {
            DiagnosticsLogList.Items.Add(BuildLogRow(log, compact: false));
        }
    }

    private UIElement BuildLogRow(ExecutionLogEntry log, bool compact)
    {
        var panel = new StackPanel { Margin = new Thickness(2, 2, 2, 4) };
        var top = new DockPanel();
        var badge = new Border
        {
            CornerRadius = new CornerRadius(4),
            Padding = new Thickness(6, 1, 6, 1),
            Margin = new Thickness(0, 0, 8, 0),
            Background = log.Success ? new SolidColorBrush(Color.FromRgb(22, 56, 36)) : new SolidColorBrush(Color.FromRgb(74, 24, 29))
        };
        badge.Child = new TextBlock
        {
            Text = log.Success ? $"OK ({log.ExitCode})" : $"ERR ({log.ExitCode})",
            FontSize = 11,
            FontWeight = FontWeights.SemiBold,
            Foreground = log.Success ? (Brush)FindResource("SuccessBrush") : (Brush)FindResource("DangerBrush")
        };
        DockPanel.SetDock(badge, Dock.Left);
        top.Children.Add(badge);

        var title = new TextBlock { Text = log.Title, FontWeight = FontWeights.SemiBold, VerticalAlignment = VerticalAlignment.Center };
        top.Children.Add(title);

        var meta = new TextBlock
        {
            Text = $"{log.DurationMs}ms  •  {log.ConsoleVisibility}  •  {log.TimestampUtc}",
            FontSize = 11,
            Foreground = (Brush)FindResource("TextMutedBrush"),
            HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center
        };
        top.Children.Add(meta);
        panel.Children.Add(top);

        if (!compact)
        {
            if (!string.IsNullOrWhiteSpace(log.Executable))
            {
                panel.Children.Add(new TextBlock
                {
                    Text = $"{log.Executable} {log.Arguments}".Trim(),
                    FontSize = 11,
                    FontFamily = new FontFamily("Cascadia Code, Consolas"),
                    Foreground = (Brush)FindResource("TextSecondaryBrush"),
                    TextTrimming = TextTrimming.CharacterEllipsis,
                    Margin = new Thickness(0, 3, 0, 0)
                });
            }
            if (!string.IsNullOrWhiteSpace(log.ErrorMessage))
            {
                panel.Children.Add(new TextBlock
                {
                    Text = log.ErrorMessage,
                    FontSize = 11,
                    Foreground = (Brush)FindResource("DangerBrush"),
                    TextWrapping = TextWrapping.Wrap,
                    Margin = new Thickness(0, 2, 0, 0)
                });
            }
            if (!string.IsNullOrWhiteSpace(log.OutputSnippet))
            {
                panel.Children.Add(new TextBlock
                {
                    Text = log.OutputSnippet.Trim(),
                    FontSize = 11,
                    FontFamily = new FontFamily("Cascadia Code, Consolas"),
                    Foreground = (Brush)FindResource("TextMutedBrush"),
                    TextWrapping = TextWrapping.Wrap,
                    MaxHeight = 64,
                    Margin = new Thickness(0, 2, 0, 0)
                });
            }
        }
        return panel;
    }

    private void RefreshLogs_Click(object sender, RoutedEventArgs e) => RefreshLogs();

    // =========================================================================
    // 2. Menu Designer (Drag & Drop + Categories + Live Preview)
    // =========================================================================

    private sealed class DesignerRow
    {
        public MenuItemConfig Item { get; init; } = new();
        public int Depth { get; init; }
    }

    private List<DesignerRow> BuildOrderedHierarchy()
    {
        var result = new List<DesignerRow>();
        var visited = new HashSet<string>();

        void Walk(string parentId, int depth)
        {
            var kids = _config.Items.Where(i => i.ParentId == parentId).OrderBy(i => i.Order).ToList();
            foreach (var k in kids)
            {
                if (!visited.Add(k.Id)) continue;
                result.Add(new DesignerRow { Item = k, Depth = depth });
                if (k.Kind == "category")
                {
                    Walk(k.Id, depth + 1);
                }
            }
        }

        Walk("", 0);
        foreach (var orphan in _config.Items.Where(i => !visited.Contains(i.Id)))
        {
            result.Add(new DesignerRow { Item = orphan, Depth = 0 });
        }
        return result;
    }

    private void RefreshDesignerList()
    {
        string? prevId = (DesignerList.SelectedItem as ListBoxItem)?.Tag as string;
        DesignerList.Items.Clear();

        foreach (var row in BuildOrderedHierarchy())
        {
            var item = row.Item;
            var lbi = new ListBoxItem { Tag = item.Id };
            var grid = new Grid { Margin = new Thickness(row.Depth * 22, 0, 0, 0) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            string glyph = item.Kind switch
            {
                "category" => "\uE8B7",
                "separator" => "\uE949",
                _ => item.BuiltIn ? "\uE756" : "\uE943"
            };
            var iconTb = new TextBlock
            {
                Text = glyph,
                FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets"),
                FontSize = 13,
                Foreground = item.Kind == "category" ? (Brush)FindResource("AccentBrush") : (Brush)FindResource("TextSecondaryBrush"),
                Margin = new Thickness(0, 0, 10, 0),
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(iconTb, 0);
            grid.Children.Add(iconTb);

            var titleTb = new TextBlock
            {
                Text = item.Kind == "separator" ? "────────── Separator ──────────" : item.Title,
                FontWeight = item.Kind == "category" ? FontWeights.SemiBold : FontWeights.Normal,
                Foreground = item.Enabled ? (Brush)FindResource("TextPrimaryBrush") : (Brush)FindResource("TextMutedBrush"),
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(titleTb, 1);
            grid.Children.Add(titleTb);

            string modTag = item.Rules.ModifierKey switch
            {
                "ctrl" => " [Ctrl]",
                "shift" => " [Shift]",
                "alt" => " [Alt]",
                "ctrl_shift" => " [Ctrl+Shift]",
                _ => ""
            };
            var statusTb = new TextBlock
            {
                Text = (!item.Enabled ? "Disabled" : (item.Kind == "category" ? "Category" : (item.BuiltIn ? "Built-in" : "Custom"))) + modTag,
                FontSize = 11,
                Foreground = string.IsNullOrEmpty(modTag) ? (Brush)FindResource("TextMutedBrush") : (Brush)FindResource("AccentBrush"),
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(statusTb, 2);
            grid.Children.Add(statusTb);

            lbi.Content = grid;
            DesignerList.Items.Add(lbi);
            if (item.Id == prevId) DesignerList.SelectedItem = lbi;
        }
    }

    private MenuItemConfig? GetSelectedDesignerItem()
    {
        if (DesignerList.SelectedItem is not ListBoxItem lbi || lbi.Tag is not string id) return null;
        return _config.Items.FirstOrDefault(i => i.Id == id);
    }

    private void DesignerList_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _dragStartPoint = e.GetPosition(null);
    }

    private void DesignerList_PreviewMouseMove(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed) return;
        Point pos = e.GetPosition(null);
        if (Math.Abs(pos.X - _dragStartPoint.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(pos.Y - _dragStartPoint.Y) < SystemParameters.MinimumVerticalDragDistance)
            return;

        if (DesignerList.SelectedItem is ListBoxItem lbi && lbi.Tag is string id)
        {
            DragDrop.DoDragDrop(DesignerList, id, DragDropEffects.Move);
        }
    }

    private void DesignerList_Drop(object sender, DragEventArgs e)
    {
        if (!e.Data.GetDataPresent(DataFormats.StringFormat)) return;
        string draggedId = (string)e.Data.GetData(DataFormats.StringFormat);
        var dragged = _config.Items.FirstOrDefault(i => i.Id == draggedId);
        if (dragged == null) return;

        var targetElem = e.OriginalSource as DependencyObject;
        while (targetElem != null && targetElem is not ListBoxItem)
            targetElem = VisualTreeHelper.GetParent(targetElem);

        if (targetElem is ListBoxItem targetLbi && targetLbi.Tag is string targetId && targetId != draggedId)
        {
            var target = _config.Items.FirstOrDefault(i => i.Id == targetId);
            if (target == null) return;

            _config.Items.Remove(dragged);
            int targetIdx = _config.Items.IndexOf(target);
            dragged.ParentId = target.Kind == "category" ? target.Id : target.ParentId;
            _config.Items.Insert(targetIdx, dragged);
            SaveConfigWithFeedback("Reordered menu");
            RefreshDesignerList();
        }
    }

    private void DesignerAddCategory_Click(object sender, RoutedEventArgs e)
    {
        var name = DarkDialog.Prompt(this, "New Category", "Enter a name for the submenu category:", "Developer Tools");
        if (string.IsNullOrWhiteSpace(name)) return;
        var cat = new MenuItemConfig
        {
            Id = "cat." + Guid.NewGuid().ToString("N")[..8],
            Kind = "category",
            Title = name.Trim(),
            Icon = @"Icons\System Folders.ico",
            Order = _config.Items.Count
        };
        _config.Items.Add(cat);
        SaveConfigWithFeedback("Added category");
        RefreshDesignerList();
        RefreshCustomCommandsList();
    }

    private void DesignerAddSeparator_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        var sep = new MenuItemConfig
        {
            Id = "sep." + Guid.NewGuid().ToString("N")[..8],
            Kind = "separator",
            ParentId = sel?.ParentId ?? "",
            Title = "---",
            Order = _config.Items.Count
        };
        int idx = sel != null ? _config.Items.IndexOf(sel) + 1 : _config.Items.Count;
        _config.Items.Insert(idx, sep);
        SaveConfigWithFeedback("Added separator");
        RefreshDesignerList();
    }

    private void DesignerMoveUp_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (sel == null) return;
        int idx = _config.Items.IndexOf(sel);
        if (idx > 0)
        {
            _config.Items.RemoveAt(idx);
            _config.Items.Insert(idx - 1, sel);
            SaveConfigWithFeedback("Moved up");
            RefreshDesignerList();
        }
    }

    private void DesignerMoveDown_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (sel == null) return;
        int idx = _config.Items.IndexOf(sel);
        if (idx >= 0 && idx < _config.Items.Count - 1)
        {
            _config.Items.RemoveAt(idx);
            _config.Items.Insert(idx + 1, sel);
            SaveConfigWithFeedback("Moved down");
            RefreshDesignerList();
        }
    }

    private void DesignerNest_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (sel == null) return;
        var categories = _config.Items.Where(i => i.Kind == "category" && i.Id != sel.Id).ToList();
        if (categories.Count == 0)
        {
            DarkDialog.ShowInfo(this, "No Categories", "Create a category first using '+ Add Category'.");
            return;
        }
        // Nest into the nearest preceding category or the first available category
        int idx = _config.Items.IndexOf(sel);
        var targetCat = _config.Items.Take(idx).LastOrDefault(i => i.Kind == "category" && i.Id != sel.Id) ?? categories[0];
        sel.ParentId = targetCat.Id;
        SaveConfigWithFeedback($"Nested into '{targetCat.Title}'");
        RefreshDesignerList();
    }

    private void DesignerUnnest_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (sel == null) return;
        sel.ParentId = "";
        SaveConfigWithFeedback("Moved to root");
        RefreshDesignerList();
    }

    private void DesignerToggleEnabled_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (sel == null) return;
        sel.Enabled = !sel.Enabled;
        SaveConfigWithFeedback(sel.Enabled ? "Enabled item" : "Disabled item");
        RefreshDesignerList();
        RefreshBuiltInCommands();
    }

    private bool _updatingDesignerModifier;

    private void DesignerList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (DesignerModifierCombo == null) return;
        _updatingDesignerModifier = true;
        DesignerModifierCombo.IsEnabled = sel != null && sel.Kind != "separator";
        SelectComboByTag(DesignerModifierCombo, sel?.Rules.ModifierKey ?? "none");
        _updatingDesignerModifier = false;
    }

    private void DesignerModifier_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingDesignerModifier) return;
        var sel = GetSelectedDesignerItem();
        if (sel == null || sel.Kind == "separator") return;
        string mod = GetSelectedComboTag(DesignerModifierCombo, "none");
        if (sel.Rules.ModifierKey == mod) return;
        sel.Rules.ModifierKey = mod;
        SaveConfigWithFeedback($"Updated modifier for '{sel.Title}'");
        RefreshDesignerList();
        RefreshBuiltInCommands();
    }

    private void DesignerDelete_Click(object sender, RoutedEventArgs e)
    {
        var sel = GetSelectedDesignerItem();
        if (sel == null) return;
        if (sel.BuiltIn && sel.Kind == "command")
        {
            DarkDialog.ShowInfo(this, "Built-in Command", "Built-in commands cannot be deleted; use 'Toggle Enabled' to hide them.");
            return;
        }
        if (!DarkDialog.Confirm(this, "Delete Item", $"Delete '{sel.Title}'? Any nested items will be moved to root.")) return;
        foreach (var child in _config.Items.Where(i => i.ParentId == sel.Id)) child.ParentId = "";
        _config.Items.Remove(sel);
        SaveConfigWithFeedback("Deleted item");
        RefreshDesignerList();
        RefreshCustomCommandsList();
    }

    private void PreviewContext_Changed(object sender, SelectionChangedEventArgs e) => RefreshLivePreview();

    private void RefreshLivePreview()
    {
        if (LivePreviewPanel == null) return;
        LivePreviewPanel.Children.Clear();

        string ctxMode = ((PreviewContextCombo.SelectedItem as ComboBoxItem)?.Tag as string) ?? "file";
        string simMod = ((PreviewModifierCombo?.SelectedItem as ComboBoxItem)?.Tag as string) ?? "none";
        bool ctrlHeld = simMod is "ctrl" or "ctrl_shift";
        bool shiftHeld = simMod is "shift" or "ctrl_shift";
        bool altHeld = simMod == "alt";

        bool MatchesMod(MenuItemConfig item)
        {
            return item.Rules.ModifierKey switch
            {
                "ctrl" => ctrlHeld,
                "shift" => shiftHeld,
                "alt" => altHeld,
                "ctrl_shift" => ctrlHeld && shiftHeld,
                _ => true
            };
        }

        bool Matches(MenuItemConfig item)
        {
            if (!item.Enabled) return false;
            if (item.Kind == "separator") return true;
            if (!MatchesMod(item)) return false;
            if (item.Kind != "command") return true;
            if (item.BuiltIn && item.BuiltInAction == "settings") return true;
            return ctxMode switch
            {
                "folder" => item.Rules.ShowOnFolders,
                "background" => item.Rules.ShowOnBackground,
                _ => item.Rules.ShowOnFiles
            };
        }

        var header = new TextBlock
        {
            Text = $"{_config.Appearance.RootMenuTitle}  \u25B8",
            FontWeight = FontWeights.SemiBold,
            Foreground = (Brush)FindResource("AccentBrush"),
            Margin = new Thickness(6, 4, 6, 8)
        };
        LivePreviewPanel.Children.Add(header);

        bool flatten = _config.Appearance.ModernSubmenuMode == "flatten";
        var topItems = _config.Items.Where(i => i.Enabled && string.IsNullOrEmpty(i.ParentId)).OrderBy(i => i.Order).ToList();

        void AddPreviewRow(string text, bool isCategory, int indent = 0)
        {
            var bd = new Border
            {
                CornerRadius = new CornerRadius(5),
                Padding = new Thickness(10 + indent * 14, 6, 10, 6),
                Margin = new Thickness(2, 1, 2, 1),
                Background = isCategory ? new SolidColorBrush(Color.FromRgb(26, 32, 46)) : Brushes.Transparent
            };
            bd.Child = new TextBlock
            {
                Text = isCategory ? $"{text}   \u25B8" : text,
                FontSize = 12.5,
                Foreground = (Brush)FindResource("TextPrimaryBrush")
            };
            LivePreviewPanel.Children.Add(bd);
        }

        void AddSepRow()
        {
            LivePreviewPanel.Children.Add(new Border { Height = 1, Background = (Brush)FindResource("BorderSubtleBrush"), Margin = new Thickness(8, 5, 8, 5) });
        }

        foreach (var item in topItems)
        {
            if (item.Kind == "separator")
            {
                AddSepRow();
            }
            else if (item.Kind == "category")
            {
                if (!MatchesMod(item)) continue;
                var kids = _config.Items.Where(k => k.ParentId == item.Id && k.Kind == "command" && Matches(k)).OrderBy(k => k.Order).ToList();
                if (kids.Count == 0) continue;
                if (flatten)
                {
                    AddSepRow();
                    foreach (var k in kids) AddPreviewRow(k.Title, false);
                    AddSepRow();
                }
                else
                {
                    AddPreviewRow(item.Title, true);
                    foreach (var k in kids) AddPreviewRow(k.Title, false, indent: 1);
                }
            }
            else if (Matches(item))
            {
                AddPreviewRow(item.Title, false);
            }
        }
    }

    // =========================================================================
    // 3. Built-in Commands
    // =========================================================================

    private void RefreshBuiltInCommands()
    {
        BuiltInCommandsPanel.Children.Clear();
        foreach (var item in _config.Items.Where(i => i.BuiltIn && i.Kind == "command").OrderBy(i => i.Order))
        {
            var card = new Border { Style = (Style)FindResource("CardBorder"), Padding = new Thickness(14, 10, 14, 10), Margin = new Thickness(0, 0, 0, 8) };
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var left = new StackPanel();
            left.Children.Add(new TextBlock { Text = item.Title, FontWeight = FontWeights.SemiBold, FontSize = 13.5 });
            left.Children.Add(new TextBlock
            {
                Text = $"{item.Description}  •  Targets: {(item.Rules.ShowOnFiles ? "Files " : "")}{(item.Rules.ShowOnFolders ? "Folders " : "")}{(item.Rules.ShowOnBackground ? "Background" : "")}",
                FontSize = 12,
                Foreground = (Brush)FindResource("TextSecondaryBrush"),
                Margin = new Thickness(0, 2, 0, 0)
            });
            Grid.SetColumn(left, 0);
            grid.Children.Add(left);

            var modCombo = new ComboBox { Width = 128, Margin = new Thickness(10, 0, 14, 0), VerticalAlignment = VerticalAlignment.Center, ToolTip = "Modifier key required to show this command" };
            modCombo.Items.Add(new ComboBoxItem { Content = "Always", Tag = "none" });
            modCombo.Items.Add(new ComboBoxItem { Content = "Hold Ctrl", Tag = "ctrl" });
            modCombo.Items.Add(new ComboBoxItem { Content = "Hold Shift", Tag = "shift" });
            modCombo.Items.Add(new ComboBoxItem { Content = "Hold Alt", Tag = "alt" });
            modCombo.Items.Add(new ComboBoxItem { Content = "Ctrl + Shift", Tag = "ctrl_shift" });
            SelectComboByTag(modCombo, item.Rules.ModifierKey);
            modCombo.SelectionChanged += (_, _) =>
            {
                string m = GetSelectedComboTag(modCombo, "none");
                if (item.Rules.ModifierKey != m)
                {
                    item.Rules.ModifierKey = m;
                    SaveConfigWithFeedback($"Updated modifier for {item.Title}");
                    RefreshDesignerList();
                }
            };
            Grid.SetColumn(modCombo, 1);
            grid.Children.Add(modCombo);

            var toggle = new CheckBox
            {
                Style = (Style)FindResource("ToggleSwitch"),
                IsChecked = item.Enabled,
                VerticalAlignment = VerticalAlignment.Center
            };
            toggle.Checked += (_, _) => { item.Enabled = true; SaveConfigWithFeedback($"Enabled {item.Title}"); };
            toggle.Unchecked += (_, _) => { item.Enabled = false; SaveConfigWithFeedback($"Disabled {item.Title}"); };
            Grid.SetColumn(toggle, 2);
            grid.Children.Add(toggle);

            card.Child = grid;
            BuiltInCommandsPanel.Children.Add(card);
        }
    }

    // =========================================================================
    // 4. Custom Commands
    // =========================================================================

    private void RefreshCustomCommandsList()
    {
        string? prevId = _selectedCustom?.Id;
        CustomList.Items.Clear();

        CustomCategoryCombo.Items.Clear();
        CustomCategoryCombo.Items.Add(new ComboBoxItem { Content = "(Root Level)", Tag = "" });
        foreach (var cat in _config.Items.Where(i => i.Kind == "category"))
        {
            CustomCategoryCombo.Items.Add(new ComboBoxItem { Content = cat.Title, Tag = cat.Id });
        }

        foreach (var item in _config.Items.Where(i => !i.BuiltIn && i.Kind == "command").OrderBy(i => i.Order))
        {
            var lbi = new ListBoxItem { Content = item.Title, Tag = item.Id };
            CustomList.Items.Add(lbi);
            if (item.Id == prevId) CustomList.SelectedItem = lbi;
        }
    }

    private void CustomList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (CustomList.SelectedItem is not ListBoxItem lbi || lbi.Tag is not string id)
        {
            _selectedCustom = null;
            CustomEditorPanel.IsEnabled = false;
            return;
        }
        _selectedCustom = _config.Items.FirstOrDefault(i => i.Id == id);
        if (_selectedCustom == null) return;

        CustomEditorPanel.IsEnabled = true;
        CustomTitleBox.Text = _selectedCustom.Title;
        SelectComboByTag(CustomRunnerTypeCombo, _selectedCustom.Runner.Type);
        CustomExeBox.Text = _selectedCustom.Runner.Executable;
        CustomArgsBox.Text = _selectedCustom.Runner.Arguments;
        SelectComboByTag(CustomVisibilityCombo, _selectedCustom.Runner.ConsoleVisibility);
        SelectComboByTag(CustomWorkDirCombo, _selectedCustom.Runner.WorkingDirectoryMode);
        SelectComboByTag(CustomCategoryCombo, _selectedCustom.ParentId);
        CustomIconBox.Text = _selectedCustom.Icon;
        CustomShowFilesChk.IsChecked = _selectedCustom.Rules.ShowOnFiles;
        CustomShowFoldersChk.IsChecked = _selectedCustom.Rules.ShowOnFolders;
        CustomShowBgChk.IsChecked = _selectedCustom.Rules.ShowOnBackground;
        CustomAdminChk.IsChecked = _selectedCustom.Runner.RunAsAdmin;
        CustomLogChk.IsChecked = _selectedCustom.Runner.LogOutput;
        CustomExtsBox.Text = string.Join(", ", _selectedCustom.Rules.Extensions);
        SelectComboByTag(CustomModifierCombo, _selectedCustom.Rules.ModifierKey);
    }

    private void AddCustomCommand_Click(object sender, RoutedEventArgs e)
    {
        var cmd = new MenuItemConfig
        {
            Id = "custom." + Guid.NewGuid().ToString("N")[..8],
            Kind = "command",
            BuiltIn = false,
            Enabled = true,
            Title = "New Custom Command",
            Order = _config.Items.Count,
            Runner = new RunnerConfig { Type = "powershell", Executable = "Write-Host 'Selected:' {path}", ConsoleVisibility = "show" },
            Rules = new ContextRulesConfig { ShowOnFiles = true, ShowOnFolders = true, ShowOnBackground = true }
        };
        _config.Items.Add(cmd);
        _selectedCustom = cmd;
        SaveConfigWithFeedback("Created custom command");
        RefreshCustomCommandsList();
        RefreshRulesCommandList();
    }

    private void CustomSave_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedCustom == null) return;
        _selectedCustom.Title = string.IsNullOrWhiteSpace(CustomTitleBox.Text) ? "Untitled Command" : CustomTitleBox.Text.Trim();
        _selectedCustom.Runner.Type = GetSelectedComboTag(CustomRunnerTypeCombo, "executable");
        _selectedCustom.Runner.Executable = CustomExeBox.Text.Trim();
        _selectedCustom.Runner.Arguments = CustomArgsBox.Text.Trim();
        _selectedCustom.Runner.ConsoleVisibility = GetSelectedComboTag(CustomVisibilityCombo, "default");
        _selectedCustom.Runner.WorkingDirectoryMode = GetSelectedComboTag(CustomWorkDirCombo, "target");
        _selectedCustom.ParentId = GetSelectedComboTag(CustomCategoryCombo, "");
        _selectedCustom.Icon = CustomIconBox.Text.Trim();
        _selectedCustom.Rules.ShowOnFiles = CustomShowFilesChk.IsChecked == true;
        _selectedCustom.Rules.ShowOnFolders = CustomShowFoldersChk.IsChecked == true;
        _selectedCustom.Rules.ShowOnBackground = CustomShowBgChk.IsChecked == true;
        _selectedCustom.Runner.RunAsAdmin = CustomAdminChk.IsChecked == true;
        _selectedCustom.Runner.LogOutput = CustomLogChk.IsChecked == true;
        _selectedCustom.Rules.Extensions = CustomExtsBox.Text
            .Split([',', ';', ' '], StringSplitOptions.RemoveEmptyEntries)
            .Select(x => x.StartsWith('.') ? x.ToLowerInvariant() : "." + x.ToLowerInvariant())
            .Distinct()
            .ToList();
        _selectedCustom.Rules.ModifierKey = GetSelectedComboTag(CustomModifierCombo, "none");

        SaveConfigWithFeedback("Saved custom command");
        RefreshCustomCommandsList();
        RefreshDesignerList();
        RefreshRulesCommandList();
    }

    private void CustomDelete_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedCustom == null) return;
        if (!DarkDialog.Confirm(this, "Delete Custom Command", $"Delete '{_selectedCustom.Title}'?")) return;
        _config.Items.Remove(_selectedCustom);
        _selectedCustom = null;
        CustomEditorPanel.IsEnabled = false;
        SaveConfigWithFeedback("Deleted custom command");
        RefreshCustomCommandsList();
        RefreshDesignerList();
        RefreshRulesCommandList();
    }

    private async void CustomTestRun_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedCustom == null) return;
        CustomSave_Click(sender, e);
        var (ok, msg) = await ConfigService.RunHostActionAsync($"--invoke \"{_selectedCustom.Id}\" --target \"{Environment.GetFolderPath(Environment.SpecialFolder.Desktop)}\" --folder");
        RefreshLogs();
        if (!ok) DarkDialog.ShowInfo(this, "Test Run Result", msg);
    }

    private void CustomBrowseExe_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "Executables & Scripts|*.exe;*.ps1;*.bat;*.cmd|All Files|*.*" };
        if (dlg.ShowDialog() == true) CustomExeBox.Text = dlg.FileName;
    }

    private void CustomBrowseIcon_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "Icons & Executables|*.ico;*.exe;*.dll|All Files|*.*" };
        if (dlg.ShowDialog() == true) CustomIconBox.Text = dlg.FileName;
    }

    // =========================================================================
    // 5. Execution & 6. Appearance
    // =========================================================================

    private void LoadExecutionFields()
    {
        SelectComboByTag(ExecDefaultVisCombo, _config.Execution.DefaultConsoleVisibility);
        SelectComboByTag(ExecPsCombo, _config.Execution.DefaultPowerShell);
        ExecWtProfileBox.Text = _config.Execution.DefaultTerminalProfile;
        ExecTimeoutBox.Text = _config.Execution.DefaultTimeoutSeconds.ToString();
        ExecEnableLogChk.IsChecked = _config.Execution.EnableExecutionLogging;
        ExecLogHiddenChk.IsChecked = _config.Execution.LogHiddenCommandOutput;
        ExecNotifyErrChk.IsChecked = _config.Execution.NotifyOnHiddenError;
    }

    private void SaveExecution_Click(object sender, RoutedEventArgs e)
    {
        _config.Execution.DefaultConsoleVisibility = GetSelectedComboTag(ExecDefaultVisCombo, "show");
        _config.Execution.DefaultPowerShell = GetSelectedComboTag(ExecPsCombo, "auto");
        _config.Execution.DefaultTerminalProfile = ExecWtProfileBox.Text.Trim();
        if (int.TryParse(ExecTimeoutBox.Text, out int t) && t > 0) _config.Execution.DefaultTimeoutSeconds = t;
        _config.Execution.EnableExecutionLogging = ExecEnableLogChk.IsChecked == true;
        _config.Execution.LogHiddenCommandOutput = ExecLogHiddenChk.IsChecked == true;
        _config.Execution.NotifyOnHiddenError = ExecNotifyErrChk.IsChecked == true;
        SaveConfigWithFeedback("Saved execution settings");
    }

    private void LoadAppearanceFields()
    {
        AppRootTitleBox.Text = _config.Appearance.RootMenuTitle;
        SelectComboByTag(AppIconThemeCombo, _config.Appearance.IconTheme);
        SelectComboByTag(AppSubmenuModeCombo, _config.Appearance.ModernSubmenuMode);
        AppShowIconsChk.IsChecked = _config.Appearance.ShowIcons;
    }

    private void SaveAppearance_Click(object sender, RoutedEventArgs e)
    {
        _config.Appearance.RootMenuTitle = string.IsNullOrWhiteSpace(AppRootTitleBox.Text) ? "xMenuForge" : AppRootTitleBox.Text.Trim();
        _config.Appearance.IconTheme = GetSelectedComboTag(AppIconThemeCombo, "auto");
        _config.Appearance.ModernSubmenuMode = GetSelectedComboTag(AppSubmenuModeCombo, "flyout");
        _config.Appearance.ShowIcons = AppShowIconsChk.IsChecked == true;
        SaveConfigWithFeedback("Saved appearance settings");
    }

    // =========================================================================
    // 7. Context Rules
    // =========================================================================

    private void BuildCategoryRuleCheckboxes()
    {
        RuleCategoriesPanel.Children.Clear();
        foreach (var cat in ConfigService.AllFileCategories)
        {
            RuleCategoriesPanel.Children.Add(new CheckBox
            {
                Content = cat,
                Tag = cat,
                Margin = new Thickness(0, 0, 16, 8)
            });
        }
    }

    private void RefreshRulesCommandList()
    {
        string? prevId = _selectedRuleCmd?.Id;
        RulesCommandList.Items.Clear();
        foreach (var item in _config.Items.Where(i => i.Kind is "command" or "category").OrderBy(i => i.Order))
        {
            string label = item.Kind == "category" ? $"[Category] {item.Title}" : item.Title;
            var lbi = new ListBoxItem { Content = label, Tag = item.Id };
            RulesCommandList.Items.Add(lbi);
            if (item.Id == prevId) RulesCommandList.SelectedItem = lbi;
        }
    }

    private void RulesCommandList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (RulesCommandList.SelectedItem is not ListBoxItem lbi || lbi.Tag is not string id)
        {
            _selectedRuleCmd = null;
            RulesEditorPanel.IsEnabled = false;
            return;
        }
        _selectedRuleCmd = _config.Items.FirstOrDefault(i => i.Id == id);
        if (_selectedRuleCmd == null) return;

        RulesEditorPanel.IsEnabled = true;
        RulesSelectedTitle.Text = _selectedRuleCmd.Title;
        RuleFilesChk.IsChecked = _selectedRuleCmd.Rules.ShowOnFiles;
        RuleFoldersChk.IsChecked = _selectedRuleCmd.Rules.ShowOnFolders;
        RuleBgChk.IsChecked = _selectedRuleCmd.Rules.ShowOnBackground;
        RuleMultiChk.IsChecked = _selectedRuleCmd.Rules.AllowMultiSelect;
        SelectComboByTag(RuleModifierCombo, _selectedRuleCmd.Rules.ModifierKey);
        RuleExtensionsBox.Text = string.Join(", ", _selectedRuleCmd.Rules.Extensions);

        foreach (CheckBox chk in RuleCategoriesPanel.Children.OfType<CheckBox>())
        {
            string cat = chk.Tag as string ?? "";
            chk.IsChecked = _selectedRuleCmd.Rules.FileCategories.Contains(cat, StringComparer.OrdinalIgnoreCase);
        }
    }

    private void SaveContextRules_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedRuleCmd == null) return;
        _selectedRuleCmd.Rules.ShowOnFiles = RuleFilesChk.IsChecked == true;
        _selectedRuleCmd.Rules.ShowOnFolders = RuleFoldersChk.IsChecked == true;
        _selectedRuleCmd.Rules.ShowOnBackground = RuleBgChk.IsChecked == true;
        _selectedRuleCmd.Rules.AllowMultiSelect = RuleMultiChk.IsChecked == true;
        _selectedRuleCmd.Rules.ModifierKey = GetSelectedComboTag(RuleModifierCombo, "none");
        _selectedRuleCmd.Rules.FileCategories = RuleCategoriesPanel.Children
            .OfType<CheckBox>()
            .Where(c => c.IsChecked == true && c.Tag is string)
            .Select(c => (string)c.Tag)
            .ToList();
        _selectedRuleCmd.Rules.Extensions = RuleExtensionsBox.Text
            .Split([',', ';', ' '], StringSplitOptions.RemoveEmptyEntries)
            .Select(x => x.StartsWith('.') ? x.ToLowerInvariant() : "." + x.ToLowerInvariant())
            .Distinct()
            .ToList();

        SaveConfigWithFeedback($"Saved rules for '{_selectedRuleCmd.Title}'");
        RefreshBuiltInCommands();
    }

    // =========================================================================
    // 8. Integrations & 9. Diagnostics
    // =========================================================================

    private void LoadIntegrationsFields()
    {
        IntegModernChk.IsChecked = _config.Integrations.ModernContextMenuEnabled;
        IntegClassicChk.IsChecked = _config.Integrations.ClassicContextMenuEnabled;
    }

    private async void RegisterExplorer_Click(object sender, RoutedEventArgs e)
    {
        _config.Integrations.ModernContextMenuEnabled = IntegModernChk.IsChecked == true;
        _config.Integrations.ClassicContextMenuEnabled = IntegClassicChk.IsChecked == true;
        SaveConfigWithFeedback("Saved integration preferences");

        string args = "--register";
        if (_config.Integrations.ClassicContextMenuEnabled) args += " --classic";

        var (ok, msg) = await ConfigService.RunHostActionAsync(args);
        await RefreshHealthAsync();
        DarkDialog.ShowInfo(this, ok ? "Registration Complete" : "Registration Failed", msg);
    }

    private async void UnregisterExplorer_Click(object sender, RoutedEventArgs e)
    {
        if (!DarkDialog.Confirm(this, "Unregister Explorer Integration", "Remove xMenuForge from Windows Explorer? Your settings and custom commands will be preserved."))
            return;
        var (ok, msg) = await ConfigService.RunHostActionAsync("--unregister");
        await RefreshHealthAsync();
        DarkDialog.ShowInfo(this, ok ? "Unregistered" : "Unregister Error", msg);
    }

    private void RestartExplorer_Click(object sender, RoutedEventArgs e)
    {
        if (!DarkDialog.Confirm(this, "Restart Explorer", "Restart Windows Explorer now so menu changes take effect immediately?"))
            return;
        ConfigService.RestartExplorer();
    }

    private void ImportXToolsMenu_Click(object sender, RoutedEventArgs e)
    {
        int count = ConfigService.ImportFromXToolsMenu(_config);
        if (count > 0)
        {
            SaveConfigWithFeedback($"Imported {count} command(s)");
            RefreshAllViews();
            DarkDialog.ShowInfo(this, "Import Complete", $"Imported {count} custom command(s) from xToolsMenu.");
        }
        else
        {
            DarkDialog.ShowInfo(this, "Nothing to Import", "No new custom commands were found in HKCU\\Software\\xToolsMenu\\CustomCommands.");
        }
    }

    private async Task RefreshHealthAsync()
    {
        var h = await ConfigService.QueryHealthAsync();
        bool healthy = h.ModernPackageRegistered && h.ShellDllExists && h.HostExeExists;

        SidebarStatusDot.Fill = healthy ? (Brush)FindResource("SuccessBrush") : (Brush)FindResource("WarningBrush");
        SidebarStatusLabel.Text = healthy ? "Explorer Active" : "Not Registered";
        OverviewRegStatus.Text = healthy ? "Registered" : "Needs Registration";
        OverviewRegDetail.Text = healthy ? h.PackageFullName : "Click 'Register / Repair Explorer' to activate";

        HealthChecksPanel.Children.Clear();
        void AddCheck(string label, bool ok, string detail)
        {
            var sp = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 3, 0, 3) };
            sp.Children.Add(new TextBlock
            {
                Text = ok ? "\uE73E" : "\uE711",
                FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets"),
                Foreground = ok ? (Brush)FindResource("SuccessBrush") : (Brush)FindResource("DangerBrush"),
                Margin = new Thickness(0, 0, 10, 0),
                VerticalAlignment = VerticalAlignment.Center
            });
            sp.Children.Add(new TextBlock { Text = label, FontWeight = FontWeights.SemiBold, Width = 230 });
            sp.Children.Add(new TextBlock { Text = detail, Foreground = (Brush)FindResource("TextSecondaryBrush") });
            HealthChecksPanel.Children.Add(sp);
        }

        AddCheck("Modern Sparse Package", h.ModernPackageRegistered, h.ModernPackageRegistered ? h.PackageFullName : "Not registered");
        AddCheck("Modern COM Registration", h.ModernComRegistered, h.ModernComRegistered ? "HKCU CLSID registered" : "Missing");
        AddCheck("Classic Context Menu", h.ClassicHandlersRegistered, h.ClassicHandlersRegistered ? "Enabled & registered" : "Disabled");
        AddCheck("Native Binaries", h.ShellDllExists && h.HostExeExists && h.ManifestExists, h.InstallDirectory);
        AddCheck("Configuration File", h.ConfigExists, ConfigService.ConfigPath);
    }

    private async void RefreshHealth_Click(object sender, RoutedEventArgs e) => await RefreshHealthAsync();

    private void OpenDataFolder_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("explorer.exe", ConfigService.AppDataDir) { UseShellExecute = true });
    }

    private void ExportConfig_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new SaveFileDialog { FileName = "xMenuForge-config.json", Filter = "JSON Files|*.json" };
        if (dlg.ShowDialog() == true)
        {
            File.Copy(ConfigService.ConfigPath, dlg.FileName, overwrite: true);
            DarkDialog.ShowInfo(this, "Exported", $"Configuration exported to:\n{dlg.FileName}");
        }
    }

    private void ClearLogs_Click(object sender, RoutedEventArgs e)
    {
        if (File.Exists(ConfigService.ExecutionLogPath)) File.Delete(ConfigService.ExecutionLogPath);
        RefreshLogs();
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    private static void SelectComboByTag(ComboBox combo, string tag)
    {
        foreach (ComboBoxItem item in combo.Items.OfType<ComboBoxItem>())
        {
            if (string.Equals(item.Tag as string, tag, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedItem = item;
                return;
            }
        }
        if (combo.Items.Count > 0) combo.SelectedIndex = 0;
    }

    private static string GetSelectedComboTag(ComboBox combo, string fallback)
    {
        return (combo.SelectedItem as ComboBoxItem)?.Tag as string ?? fallback;
    }
}
